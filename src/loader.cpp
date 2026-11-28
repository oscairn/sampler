// SPDX-License-Identifier: AGPL-3.0-or-later
#include "loader.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <tuple>

namespace pcmshell {

namespace {

bool before(const Record& x, const Record& y) {
    return x.pack != y.pack ? x.pack->path < y.pack->path : x.entry.offset < y.entry.offset;
}

// Frames [from, from + count) of a record, interleaved, into out: read from its window when they lie in it, else
// decoded; false when the pack cannot be read.
bool readFrames(const Record& r, uint32_t from, uint32_t count, int16_t* out, Decoder& d) {
    if (from + count <= r.info.window_frames)  // little-endian 16-bit, as the core reads
        return r.pack->read(r.entry.offset + r.info.head_len + uint64_t(from) * r.info.channels * 2, out, size_t(count) * r.info.channels * 2);
    return decodeFrames(r, from, count, out, d);
}

// The bytes of a record's FLAC frames holding frames [from, from + count): for the read-ahead hint.
void hintFrames(const Record& r, uint32_t from, uint32_t count) {
    const std::vector<uint32_t>& t = *r.table;
    const size_t b0 = std::min<size_t>(from / r.info.block_size, t.size() - 1);
    const size_t b1 = std::min<size_t>((size_t(from) + count + r.info.block_size - 1) / r.info.block_size, t.size() - 1);
    willNeed(*r.pack, r.entry.offset + r.info.head_len + r.info.window_bytes + t[b0], t[b1] - t[b0]);
}

// The process's buffers in memory, by record (pack path and offset: packs are never rewritten) and frames: a waveform
// many sounds use keeps one copy of each head and loop body. Entries of buffers no sound holds any more expire.
using BufferKey = std::tuple<std::string, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t>;  // path, offset, from, count, pre, post
std::mutex cacheLock;
std::map<BufferKey, std::weak_ptr<const std::vector<int16_t>>> cache;

std::shared_ptr<const std::vector<int16_t>> cached(const BufferKey& k) {
    std::lock_guard<std::mutex> l(cacheLock);
    auto it = cache.find(k);
    return it == cache.end() ? nullptr : it->second.lock();
}
void remember(const std::vector<BufferKey>& keys, const std::vector<std::shared_ptr<const std::vector<int16_t>>>& buffers) {
    std::lock_guard<std::mutex> l(cacheLock);
    for (auto it = cache.begin(); it != cache.end();) it = it->second.expired() ? cache.erase(it) : std::next(it);
    for (size_t i = 0; i < keys.size(); ++i) cache[keys[i]] = buffers[i];
}

// The process's sounds, whose records are checked: a sample one of them holds is taken from it, record and all, while
// its pack is the one open at its path (no resolve, nothing read). Nothing else is kept: a record no sound holds is
// resolved and read again.
std::mutex heldLock;
std::vector<std::weak_ptr<const Samples>> held;

std::string hex(const uint8_t* id) {
    static const char digits[] = "0123456789abcdef";
    std::string h(64, '0');
    for (int i = 0; i < 32; ++i) { h[2 * i] = digits[id[i] >> 4]; h[2 * i + 1] = digits[id[i] & 15]; }
    return h;
}

}  // namespace

void printTrace(const PrepareTimes& p) {
    const LoadTimes& l = p.load;
    std::string hand;
    if (p.hand >= 0) { char b[32]; std::snprintf(b, sizeof b, " hand=%.2f", p.hand); hand = b; }
    std::fprintf(stderr, "oscairn-sampler: loaded %u samples: %u resolved in %.2f ms, %u from sounds held"
                         " parse=%.2f expand=%.2f held=%.2f checks=%.2f regions=%.2f alloc=%.2f reads=%.2f keep=%.2f load=%.2f compile=%.2f%s total=%.2f"
                         " wf_resolves=%u wf_resolve=%.2f wf_opens=%u wf_bytes=%llu wf_read=%.2f wf_parse=%.2f wf_match=%.2f wf_alloc=%.2f\n",
                 l.samples, l.resolves, l.resolve, l.samples - l.resolves, p.parse, p.expand, l.held, l.checks, l.regions, l.alloc, l.reads,
                 l.keep, l.total, p.compile, hand.c_str(), p.total,
                 p.ex.resolves, p.ex.resolve, p.ex.opens, (unsigned long long)p.ex.bytes, p.ex.read, p.ex.parse, p.ex.match, p.ex.alloc);
}

bool expandWaveforms(const pcm::SoundData& in, const WaveformResolver& resolve, pcm::SoundData& out, std::string& error, ExpandTimes* times) {
    ExpandTimes et;
    Lap lap;
    struct Read { std::vector<pcm::KeyBank> banks; std::vector<std::string> refs; std::vector<const char*> ptrs; };
    std::vector<Read> read(in.waveforms);
    std::vector<pcm::WaveformData> w(in.waveforms);
    std::vector<uint8_t> file(SP_WAVEFORM_MAX_BYTES + 1);  // one buffer for every waveform file
    et.alloc = lap();
    for (uint32_t i = 0; i < in.waveforms; ++i) {
        const std::string ref = "waveform:" + hex(in.waveform[i]);
        std::string path;
        lap();
        const bool resolved = resolve(ref.c_str(), path, error);
        et.resolve += lap(); ++et.resolves;
        if (!resolved) { error = ref + ": " + error; return false; }
        size_t len = 0;
        if (FILE* f = std::fopen(path.c_str(), "rb")) { len = std::fread(file.data(), 1, file.size(), f); std::fclose(f); }
        else { error = ref + ": cannot open " + path; return false; }
        et.read += lap(); ++et.opens; et.bytes += len;
        size_t samples = 0, banks = 0;
        uint8_t id[SP_SAMPLE_ID_BYTES];
        if (sp_status st = sp_waveform_check(file.data(), len, &samples, &banks)) { error = ref + ": not a valid waveform (samplepack status " + std::to_string(int(st)) + ")"; return false; }
        if (sp_waveform_id(file.data(), len, id) || std::memcmp(id, in.waveform[i], sizeof id)) { error = ref + ": the file holds another waveform"; return false; }
        Read& r = read[i];
        for (size_t k = 0; k < samples; ++k) {
            sp_waveform_sample(file.data(), len, k, id);
            r.refs.push_back("sample:" + hex(id));
        }
        for (const auto& s : r.refs) r.ptrs.push_back(s.c_str());
        for (size_t k = 0; k < banks; ++k) {
            sp_key_bank b;
            sp_waveform_bank(file.data(), len, k, &b);
            r.banks.push_back({b.key_lo, b.key_hi, b.vel_lo, b.vel_hi, b.root, b.transpose, b.loop_mode, b.sample,
                               b.tune, b.level, b.pan, b.start, b.end, b.loop_start, b.loop_end});
        }
        w[i] = {r.banks.data(), uint32_t(banks), r.ptrs.data(), uint32_t(samples)};
        et.parse += lap();
    }
    char why[256] = {};
    const bool expanded = pcm::expand(in, w.data(), out, why, sizeof why);
    et.match = lap();
    if (times) *times = et;
    if (!expanded) { error = why; return false; }
    return true;
}

Samples::~Samples() {
    auto hold = holdStreams();  // the I/O thread is between units: none reads these any more
    sources.clear();
    records.clear();
}

std::shared_ptr<const Samples> loadSamples(const pcm::SoundData& d, const SampleResolver& resolve, bool whole, std::string& error, LoadTimes* times) {
    LoadTimes lt;
    Lap lap, all;
    auto s = std::make_shared<Samples>();
    s->records.resize(d.samples);
    // the records the process's sounds hold, by sample id
    std::vector<std::shared_ptr<const Samples>> others;
    {
        std::lock_guard<std::mutex> l(heldLock);
        for (const auto& w : held) if (auto o = w.lock()) others.push_back(std::move(o));
    }
    std::vector<const Record*> byId;
    for (const auto& o : others) for (const Record& r : o->records) byId.push_back(&r);
    auto idLess = [](const Record* r, const uint8_t* id) { return std::memcmp(r->entry.id, id, SP_SAMPLE_ID_BYTES) < 0; };
    std::sort(byId.begin(), byId.end(), [](const Record* a, const Record* b) { return std::memcmp(a->entry.id, b->entry.id, SP_SAMPLE_ID_BYTES) < 0; });
    std::vector<uint32_t> order;  // the records resolved now, to check
    uint32_t resolves = 0;
    for (uint32_t i = 0; i < d.samples; ++i) {
        uint8_t id[SP_SAMPLE_ID_BYTES];
        if (sampleId(d.ref[i], id)) {
            auto it = std::lower_bound(byId.begin(), byId.end(), id, idLess);
            std::string ignored;
            if (it != byId.end() && !std::memcmp((*it)->entry.id, id, SP_SAMPLE_ID_BYTES) && openPack((*it)->pack->path, ignored) == (*it)->pack) {
                s->records[i] = **it;
                continue;
            }
        }
        lt.held += lap();
        const bool ok = resolve(d.ref[i], s->records[i], error);
        lt.resolve += lap();
        ++resolves;
        if (!ok) { error = std::string(d.ref[i]) + ": " + error; return nullptr; }
        order.push_back(i);
    }
    lt.held += lap();
    // one read-ahead batch over their preload spans (header and window), then their headers in ascending order
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return before(s->records[a], s->records[b]); });
    for (uint32_t i : order) willNeed(*s->records[i].pack, s->records[i].entry.offset, s->records[i].entry.preload);
    for (uint32_t i : order)
        if (!checkRecord(s->records[i], error)) { error = std::string(d.ref[i]) + ": " + error; return nullptr; }
    lt.checks += lap();

    // each region: what stays in memory (buffers shared between regions, and with other sounds of the process, that
    // need the same frames of the same record) and what streams
    struct Job { uint32_t sample, from, count, offset; size_t buffer; };  // frames of a record into a buffer at `offset` frames
    std::vector<Job> jobs;
    std::vector<BufferKey> keys;  // of s->pcm, for the process's cache
    std::map<BufferKey, size_t> made;
    auto buffer = [&](uint32_t sample, uint32_t from, uint32_t count, uint32_t pre, uint32_t post) {
        const Record& rec = s->records[sample];
        const BufferKey key{rec.pack->path, rec.entry.offset, from, count, pre, post};
        const uint32_t c = rec.info.channels;
        auto it = made.find(key);
        if (it == made.end()) {
            auto old = cached(key);
            Lap a;
            s->pcm.push_back(old ? old : std::make_shared<std::vector<int16_t>>((size_t(pre) + count + post) * c, int16_t(0)));
            lt.alloc += a();
            keys.push_back(key);
            it = made.emplace(key, s->pcm.size() - 1).first;
            if (!old) jobs.push_back({sample, from, count, pre, it->second});
        }
        return s->pcm[it->second]->data() + size_t(pre) * c;
    };
    s->views.resize(d.regions);
    s->sources.resize(d.regions);
    for (uint32_t i = 0; i < d.regions; ++i) {
        const pcm::Region& g = d.region[i];
        const Record& rec = s->records[g.sample];
        const sp_record_info& r = rec.info;
        // a key bank may play part of its sample: frames [start, end) of the record
        const uint32_t start = g.start, end = g.end ? g.end : r.frames;
        if (start >= end || end > r.frames) { error = std::string(d.ref[g.sample]) + ": a region's start or end lies outside its sample"; return nullptr; }
        const uint32_t frames = end - start, head = start ? kHeadFrames : std::max(kHeadFrames, r.window_frames);  // the window holds frames from 0
        pcm::SampleView& v = s->views[i];
        v.frames = frames; v.channels = uint8_t(r.channels); v.rate = float(r.rate);
        const bool streams = !whole && (r.block_size == 256 || r.block_size == 512 || r.block_size == 1024)  // a unit fits a ring
                             && frames > head;
        if (!streams) {  // in memory whole, with the zero guards
            v.data = buffer(g.sample, start, frames, pcm::kGuardPre, pcm::kGuardPost);
            v.head = frames;
            continue;
        }
        v.head = head;
        v.data = buffer(g.sample, start, v.head, pcm::kGuardPre, pcm::kOverread);
        StreamSource& src = s->sources[i];
        src.record = rec; src.start = start; src.head = v.head;
        if (g.loop && g.loopEnd <= v.head) continue;  // the loop lies in the head: nothing streams
        if (g.loop && g.loopEnd - g.loopStart <= kMaxLoopBody) {  // the body kept whole; the frames before it stream once
            v.loop = buffer(g.sample, start + g.loopStart, g.loopEnd - g.loopStart, 0, pcm::kOverread);
            if (g.loopStart <= v.head) continue;
            src.stop = g.loopStart;
        } else if (g.loop) {  // too long to keep: the stream plays the loop too
            src.loop = true; src.loopStart = g.loopStart; src.loopEnd = g.loopEnd; src.stop = g.loopEnd;
        } else {
            src.stop = frames; src.tail = true;
        }
        v.stream = &src;
    }
    lt.regions += lap() - lt.alloc;
    // a second read-ahead batch for what is decoded now past the windows, then everything in ascending order
    std::sort(jobs.begin(), jobs.end(), [&](const Job& a, const Job& b) {
        const Record &x = s->records[a.sample], &y = s->records[b.sample];
        return x.pack == y.pack && x.entry.offset == y.entry.offset ? a.from < b.from : before(x, y);
    });
    for (const Job& j : jobs)
        if (j.from + j.count > s->records[j.sample].info.window_frames) hintFrames(s->records[j.sample], j.from, j.count);
    auto dec = std::make_unique<Decoder>();
    for (const Job& j : jobs) {
        auto& b = const_cast<std::vector<int16_t>&>(*s->pcm[j.buffer]);  // ours alone until it is cached below
        if (!readFrames(s->records[j.sample], j.from, j.count, b.data() + size_t(j.offset) * s->records[j.sample].info.channels, *dec)) {
            error = std::string(d.ref[j.sample]) + ": cannot read " + s->records[j.sample].pack->path;
            return nullptr;
        }
    }
    lt.reads += lap();
    remember(keys, s->pcm);
    {
        std::lock_guard<std::mutex> l(heldLock);
        held.erase(std::remove_if(held.begin(), held.end(), [](const auto& w) { return w.expired(); }), held.end());
        held.push_back(s);
    }
    lt.keep += lap();
    if (times) { lt.samples = d.samples; lt.resolves = resolves; lt.total = all(); *times = lt; }
    return s;
}

}  // namespace pcmshell
