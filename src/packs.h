// SPDX-License-Identifier: AGPL-3.0-or-later
// Oscairn Sample Packs for the loader (the format: third_party/samplepack/docs/format.md). Each pack file is mapped
// once per process, read-only, and shared by every instance and sound that uses it; samplepack checks every record
// before use and decodes its frames. Mapping is main-thread work; decoding may run on any thread.
#pragma once
#include <samplepack.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pcmshell {

// A pack file mapped read-only: its committed bytes (a torn tail the host is still writing is left out).
struct Pack {
    std::string path;
    const uint8_t* data = nullptr;
    size_t len = 0;     // committed bytes
    std::vector<sp_index_entry> index;  // its trailer's entries, sorted by sample id
    void* base = nullptr;
    size_t mapped = 0;  // bytes mapped
    Pack() = default;
    Pack(const Pack&) = delete;
    ~Pack();
};

// One record: its pack, the entry samplepack checks it against on every call, and its header.
struct Record {
    std::shared_ptr<const Pack> pack;
    sp_index_entry entry{};
    sp_record_info info{};
};

// A sample reference "sample:<64 lowercase hex digits>": its id. False for any other reference.
bool sampleId(const char* ref, uint8_t id[SP_SAMPLE_ID_BYTES]);

// The record of sample `id` at `offset` of the pack at `path`, from the pack's trailer, or from `length` and `preload`
// when the host gave them (non-zero). Nothing of the record is read yet: checkRecord does that.
bool findRecord(const std::string& path, uint64_t offset, const uint8_t* id, uint64_t length, uint64_t preload, Record& out, std::string& error);
// The pack at `path`, mapped (or the mapping this process already holds): its trailer's entries are in index.
std::shared_ptr<const Pack> openPack(const std::string& path, std::string& error);
// The one live record of a one-record pack (what a path reference names).
bool onlyRecord(const std::string& path, Record& out, std::string& error);
// Reads and checks a found record's header (samplepack: checksum, every field, agreement with the entry).
bool checkRecord(Record&, std::string& error);

// Working memory for decoding (about 48 KiB): one per thread that decodes.
struct Decoder {
    sp_flac_scratch scratch{};
    int16_t ch[2][SP_FLAC_MAX_BLOCK]{};
};
// Block `block` of a record into d.ch (planar); its frames, or 0 when it does not decode (the caller plays silence).
uint32_t decodeBlock(const Record&, uint32_t block, Decoder& d);
// Frames [from, from + count) of a record, interleaved 16-bit, into out (count x channels values); a block that does not
// decode gives silence.
void decodeFrames(const Record&, uint32_t from, uint32_t count, int16_t* out, Decoder& d);

// Read-ahead (WILLNEED) for bytes [offset, offset + length) of a pack: issued at once for every span a sound needs, in
// ascending order, before anything is touched (docs/format.md, "Ordering and locality"). No effect on Windows.
void willNeed(const Pack&, uint64_t offset, uint64_t length);

}  // namespace pcmshell
