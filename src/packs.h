// SPDX-License-Identifier: AGPL-3.0-or-later
// Oscairn Sample Packs for the loader (the format: third_party/samplepack/docs/format.md). Each pack file is opened
// once per process, read-only, and shared by every instance and sound that uses it; it is never mapped: its header,
// trailer and index, the record heads and the frames are read at their offsets (pread; ReadFile at an offset on
// Windows), so a process whose mappings are all locked holds only what it read. samplepack checks every
// record before use and decodes its frames. Opening is main-thread work; reading and decoding may run on any thread.
#pragma once
#include <samplepack.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pcmshell {

// A pack file open read-only: its committed length (a torn tail the host is still writing is left out) and index
// (56 bytes a record on the heap: 56,000 bytes per 1,000 records).
struct Pack {
    std::string path;
    uint64_t len = 0;   // committed bytes
    std::vector<sp_index_entry> index;  // its trailer's entries, sorted by sample id
#ifdef _WIN32
    void* file = nullptr;  // HANDLE
#else
    int fd = -1;
#endif
    Pack() = default;
    Pack(const Pack&) = delete;
    ~Pack();
    // Bytes [offset, offset + n) into out, whole; false on an error or a short read (EINTR is retried).
    bool read(uint64_t offset, void* out, size_t n) const;
};

// One record: its pack, its entry, and its header and frame table once checkRecord has read them.
struct Record {
    std::shared_ptr<const Pack> pack;
    sp_index_entry entry{};
    sp_record_info info{};
    std::shared_ptr<const std::vector<uint32_t>> table;  // frame table: blocks + 1 offsets into the frames
};

// A sample reference "sample:<64 lowercase hex digits>": its id. False for any other reference.
bool sampleId(const char* ref, uint8_t id[SP_SAMPLE_ID_BYTES]);

// The record of sample `id` at `offset` of the pack at `path`, from the pack's trailer, or from `length` and `preload`
// when the host gave them (non-zero). Nothing of the record is read yet: checkRecord does that.
bool findRecord(const std::string& path, uint64_t offset, const uint8_t* id, uint64_t length, uint64_t preload, Record& out, std::string& error);
// The pack at `path`, open (or the one this process already holds): its trailer's entries are in index.
std::shared_ptr<const Pack> openPack(const std::string& path, std::string& error);
// The one live record of a one-record pack (what a path reference names).
bool onlyRecord(const std::string& path, Record& out, std::string& error);
// Reads a found record's header and frame table (not its window) and checks them (samplepack: checksum, every field,
// frame table, agreement with the entry); keeps its frame table, and the record as checked for the process.
bool checkRecord(Record&, std::string& error);
// A found record as checkRecord checked it earlier in this process (the same entry, in the same open pack, its frame
// table still held by a sound): its header and frame table, nothing read. False when it must be checked.
bool knownRecord(Record&);

// The largest FLAC frame read. samplepack states no limit (a record states its own largest frame, checked against its
// frame table), so this is derived from SP_FLAC_MAX_BLOCK: a 4,096-frame stereo block coded verbatim (17-bit side
// channel) takes about 16,920 bytes, and with every residual escaped at 31 bits and a partition a sample about 42,044; a
// record whose largest frame exceeds the buffer is refused at prepare.
constexpr size_t kMaxFrameBytes = 48 * 1024;
// Working memory for decoding (about 96 KiB): one per thread that decodes.
struct Decoder {
    sp_flac_scratch scratch{};
    int16_t ch[2][SP_FLAC_MAX_BLOCK]{};
    uint8_t bytes[kMaxFrameBytes]{};  // the compressed block, read here before decoding
};
// Block `block` of a record into d.ch (planar): its frames; 0 when it does not decode (the caller plays silence); -1
// when it cannot be read (an I/O error or short read).
int32_t decodeBlock(const Record&, uint32_t block, Decoder& d);
// Frames [from, from + count) of a record, interleaved 16-bit, into out (count x channels values); a block that does not
// decode gives silence; false when a block cannot be read.
bool decodeFrames(const Record&, uint32_t from, uint32_t count, int16_t* out, Decoder& d);

// A test hook: every read of a pack fails while on (as an I/O error would).
void failReads(bool on);
// A test hook: bytes read from packs in this process so far.
uint64_t packBytesRead();

// Read-ahead (WILLNEED) for bytes [offset, offset + length) of a pack: issued at once for every span a sound needs, in
// ascending order, before anything is read (docs/format.md, "Ordering and locality"): posix_fadvise WILLNEED on Linux,
// fcntl F_RDADVISE on macOS; no effect on Windows.
void willNeed(const Pack&, uint64_t offset, uint64_t length);

}  // namespace pcmshell
