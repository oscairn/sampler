// SPDX-License-Identifier: AGPL-3.0-or-later
#include "loader.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <tuple>

namespace pcmshell {

namespace {

bool before(const Record& x, const Record& y) {
    return x.pack != y.pack ? x.pack->path < y.pack->path : x.entry.offset < y.entry.offset;
}

// Frames [from, from + count) of a record, interleaved, into out: from its window when they lie in it, else decoded.
void readFrames(const Record& r, uint32_t from, uint32_t count, int16_t* out, Decoder& d) {
    const uint8_t* w = nullptr; size_t bytes = 0;
    if (from + count <= r.info.window_frames && sp_record_window(r.pack->data, r.pack->len, &r.entry, &w, &bytes) == SP_OK)
        std::memcpy(out, w + size_t(from) * r.info.channels * 2, size_t(count) * r.info.channels * 2);  // little-endian 16-bit, as the core reads
    else decodeFrames(r, from, count, out, d);
}

// The bytes of a record's FLAC frames holding frames [from, from + count): for the read-ahead hint.
void hintFrames(const Record& r, uint32_t from, uint32_t count) {
    const uint8_t *frames = nullptr, *table = nullptr; size_t len = 0, entries = 0;
    if (sp_record_frames(r.pack->data, r.pack->len, &r.entry, &frames, &len, &table, &entries) != SP_OK) return;
    auto at = [&](size_t i) { const uint8_t* t = table + 4 * std::min(i, entries - 1); return uint32_t(t[0]) | uint32_t(t[1]) << 8 | uint32_t(t[2]) << 16 | uint32_t(t[3]) << 24; };
    const size_t b0 = from / r.info.block_size, b1 = (size_t(from) + count + r.info.block_size - 1) / r.info.block_size;
    willNeed(*r.pack, uint64_t(frames - r.pack->data) + at(b0), at(b1) - at(b0));
}

}  // namespace

Samples::~Samples() {
    auto hold = holdStreams();  // the I/O thread is between units: none reads these any more
    sources.clear();
    records.clear();
}

std::shared_ptr<const Samples> loadSamples(const pcm::SoundData& d, const SampleResolver& resolve, bool whole, std::string& error) {
    auto s = std::make_shared<Samples>();
    s->records.resize(d.samples);
    for (uint32_t i = 0; i < d.samples; ++i)
        if (!resolve(d.ref[i], s->records[i], error)) { error = std::string(d.ref[i]) + ": " + error; return nullptr; }
    // one read-ahead batch over every record's preload span (header and window), then the headers in ascending order
    std::vector<uint32_t> order(d.samples);
    for (uint32_t i = 0; i < d.samples; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return before(s->records[a], s->records[b]); });
    for (uint32_t i : order) willNeed(*s->records[i].pack, s->records[i].entry.offset, s->records[i].entry.preload);
    for (uint32_t i : order)
        if (!checkRecord(s->records[i], error)) { error = std::string(d.ref[i]) + ": " + error; return nullptr; }

    // each region: what stays in memory (buffers shared between regions that need the same frames) and what streams
    struct Job { uint32_t sample, from, count, offset; size_t buffer; };  // frames of a record into a buffer at `offset` frames
    std::vector<Job> jobs;
    std::map<std::array<uint32_t, 5>, size_t> made;
    auto buffer = [&](uint32_t sample, uint32_t from, uint32_t count, uint32_t pre, uint32_t post) {
        const auto key = std::array<uint32_t, 5>{sample, from, count, pre, post};
        if (auto it = made.find(key); it != made.end()) return s->pcm[it->second].data() + size_t(pre) * s->records[sample].info.channels;
        const uint32_t c = s->records[sample].info.channels;
        s->pcm.emplace_back((size_t(pre) + count + post) * c, int16_t(0));
        made[key] = s->pcm.size() - 1;
        jobs.push_back({sample, from, count, pre, s->pcm.size() - 1});
        return s->pcm.back().data() + size_t(pre) * c;
    };
    s->views.resize(d.regions);
    s->sources.resize(d.regions);
    for (uint32_t i = 0; i < d.regions; ++i) {
        const pcm::Region& g = d.region[i];
        const Record& rec = s->records[g.sample];
        const sp_record_info& r = rec.info;
        const uint32_t frames = r.frames, start = 0;
        pcm::SampleView& v = s->views[i];
        v.frames = frames; v.channels = uint8_t(r.channels); v.rate = float(r.rate);
        const bool streams = !whole && (r.block_size == 256 || r.block_size == 512 || r.block_size == 1024)  // a unit fits a ring
                             && frames > std::max(kHeadFrames, r.window_frames);
        if (!streams) {  // in memory whole, with the zero guards
            v.data = buffer(g.sample, start, frames, pcm::kGuardPre, pcm::kGuardPost);
            v.head = frames;
            continue;
        }
        v.head = std::max(kHeadFrames, r.window_frames);
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
        auto& b = s->pcm[j.buffer];
        readFrames(s->records[j.sample], j.from, j.count, b.data() + size_t(j.offset) * s->records[j.sample].info.channels, *dec);
        s->bytes += b.size() * sizeof(int16_t);
    }
    return s;
}

}  // namespace pcmshell
