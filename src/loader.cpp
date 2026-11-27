// SPDX-License-Identifier: AGPL-3.0-or-later
#include "loader.h"

#include <algorithm>
#include <array>
#include <cstdio>
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

// Where this process found each sample id (as the host resolved it and checkRecord confirmed), so that a sound
// prepared again asks the host only for samples no sound holds. An entry counts while a sound holds its record's frame
// table; one gone stale (another pack put at that path since) fails findRecord or checkRecord's id check, and the sample
// is resolved again.
struct Where { std::string path; sp_index_entry entry; std::weak_ptr<const std::vector<uint32_t>> table; };
std::mutex whereLock;
std::map<std::string, Where> where;  // by reference "sample:<id>"

bool located(const char* ref, Record& r) {
    uint8_t id[SP_SAMPLE_ID_BYTES];
    if (!sampleId(ref, id)) return false;
    Where w;
    {
        std::lock_guard<std::mutex> l(whereLock);
        auto it = where.find(ref);
        if (it == where.end() || it->second.table.expired()) return false;
        w = it->second;
    }
    std::string ignored;
    return findRecord(w.path, w.entry.offset, id, w.entry.length, w.entry.preload, r, ignored);
}
void locate(const pcm::SoundData& d, const std::vector<Record>& records) {
    uint8_t id[SP_SAMPLE_ID_BYTES];
    std::lock_guard<std::mutex> l(whereLock);
    for (auto it = where.begin(); it != where.end();) it = it->second.table.expired() ? where.erase(it) : std::next(it);
    for (uint32_t i = 0; i < d.samples; ++i)
        if (sampleId(d.ref[i], id)) where[d.ref[i]] = {records[i].pack->path, records[i].entry, records[i].table};
}

std::string hex(const uint8_t* id) {
    std::string h(64, '0');
    for (int i = 0; i < 32; ++i) std::snprintf(&h[2 * i], 3, "%02x", id[i]);
    return h;
}

}  // namespace

bool expandWaveforms(const pcm::SoundData& in, const WaveformResolver& resolve, pcm::SoundData& out, std::string& error) {
    struct Read { std::vector<pcm::KeyBank> banks; std::vector<std::string> refs; std::vector<const char*> ptrs; };
    std::vector<Read> read(in.waveforms);
    std::vector<pcm::WaveformData> w(in.waveforms);
    for (uint32_t i = 0; i < in.waveforms; ++i) {
        const std::string ref = "waveform:" + hex(in.waveform[i]);
        std::string path;
        if (!resolve(ref.c_str(), path, error)) { error = ref + ": " + error; return false; }
        std::vector<uint8_t> file(SP_WAVEFORM_MAX_BYTES + 1);
        size_t len = 0;
        if (FILE* f = std::fopen(path.c_str(), "rb")) { len = std::fread(file.data(), 1, file.size(), f); std::fclose(f); }
        else { error = ref + ": cannot open " + path; return false; }
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
    }
    char why[256] = {};
    if (!pcm::expand(in, w.data(), out, why, sizeof why)) { error = why; return false; }
    return true;
}

Samples::~Samples() {
    auto hold = holdStreams();  // the I/O thread is between units: none reads these any more
    sources.clear();
    records.clear();
}

std::shared_ptr<const Samples> loadSamples(const pcm::SoundData& d, const SampleResolver& resolve, bool whole, std::string& error) {
    auto s = std::make_shared<Samples>();
    s->records.resize(d.samples);
    std::vector<bool> known(d.samples);  // found where this process found it before, without asking the host
    for (uint32_t i = 0; i < d.samples; ++i)
        if (!(known[i] = located(d.ref[i], s->records[i])) && !resolve(d.ref[i], s->records[i], error)) { error = std::string(d.ref[i]) + ": " + error; return nullptr; }
    // the records not checked yet: one read-ahead batch over their preload spans (header and window), then their
    // headers in ascending order; a record found where it was before and no longer there is resolved again
    std::vector<uint32_t> order;
    for (uint32_t i = 0; i < d.samples; ++i) if (!knownRecord(s->records[i])) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return before(s->records[a], s->records[b]); });
    for (uint32_t i : order) willNeed(*s->records[i].pack, s->records[i].entry.offset, s->records[i].entry.preload);
    for (uint32_t i : order)
        if (!checkRecord(s->records[i], error) &&
            !(known[i] && resolve(d.ref[i], s->records[i], error) && (knownRecord(s->records[i]) || checkRecord(s->records[i], error)))) {
            error = std::string(d.ref[i]) + ": " + error; return nullptr;
        }

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
            s->pcm.push_back(old ? old : std::make_shared<std::vector<int16_t>>((size_t(pre) + count + post) * c, int16_t(0)));
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
    remember(keys, s->pcm);
    locate(d, s->records);
    return s;
}

}  // namespace pcmshell
