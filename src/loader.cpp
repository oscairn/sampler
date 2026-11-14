// SPDX-License-Identifier: AGPL-3.0-or-later
#include "loader.h"

#include <algorithm>

namespace pcmshell {

std::shared_ptr<const Samples> loadSamples(const pcm::SoundData& d, const SampleResolver& resolve, std::string& error) {
    auto s = std::make_shared<Samples>();
    s->records.resize(d.samples);
    for (uint32_t i = 0; i < d.samples; ++i)
        if (!resolve(d.ref[i], s->records[i], error)) { error = std::string(d.ref[i]) + ": " + error; return nullptr; }
    // one read-ahead batch over every record, then the records in ascending order of pack and offset
    std::vector<uint32_t> order(d.samples);
    for (uint32_t i = 0; i < d.samples; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        const Record &x = s->records[a], &y = s->records[b];
        return x.pack != y.pack ? x.pack->path < y.pack->path : x.entry.offset < y.entry.offset;
    });
    for (uint32_t i : order) willNeed(*s->records[i].pack, s->records[i].entry.offset, s->records[i].entry.length);
    auto dec = std::make_unique<Decoder>();
    s->pcm.resize(d.samples);
    s->views.resize(d.samples);
    for (uint32_t i : order) {
        const sp_record_info& r = s->records[i].info;
        auto& buf = s->pcm[i];
        buf.assign((size_t(pcm::kGuardPre) + r.frames + pcm::kGuardPost) * r.channels, 0);
        decodeFrames(s->records[i], 0, r.frames, buf.data() + pcm::kGuardPre * r.channels, *dec);
        s->views[i] = {buf.data() + pcm::kGuardPre * r.channels, r.frames, uint8_t(r.channels), float(r.rate)};
        s->bytes += buf.size() * sizeof(int16_t);
    }
    return s;
}

}  // namespace pcmshell
