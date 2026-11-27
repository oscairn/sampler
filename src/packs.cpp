// SPDX-License-Identifier: AGPL-3.0-or-later
#include "packs.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX  // windows.h would define min and max as macros
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace pcmshell {

namespace {

constexpr uint64_t kFooter = 32;

// Opens a file read-only; false when it cannot. Its size into `size`.
bool openFile(const std::string& path, Pack& p, uint64_t& size) {
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring w(size_t(n > 0 ? n : 1), L'\0');
    if (n <= 0 || !MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], n)) return false;
    HANDLE f = CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    p.file = f;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(f, &sz)) return false;
    size = uint64_t(sz.QuadPart);
#else
    p.fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (p.fd < 0) return false;
    struct stat st{};
    if (fstat(p.fd, &st) != 0) return false;
    size = uint64_t(st.st_size);
#endif
    return true;
}

// Checks the pack from its header page and the tail [toff, end) and reads its index; SP_OK or samplepack's status.
sp_status openTail(Pack& p, const std::vector<uint8_t>& header, uint64_t toff, uint64_t end) {
    if (end - toff > uint64_t(SP_PACK_MAX_RECORDS) * 56 + 2 * SP_PACK_PAGE) return SP_ERR_BAD_FOOTER;  // no trailer is larger
    std::vector<uint8_t> tail(size_t(end - toff));
    if (!p.read(toff, tail.data(), tail.size())) return SP_ERR_BAD_FOOTER;
    sp_pack_info info{};
    size_t n = 0;
    sp_status st = sp_pack_tail_index(header.data(), header.size(), tail.data(), tail.size(), end, &info, nullptr, 0, &n);
    if (st == SP_ERR_BUFFER_TOO_SMALL) {
        p.index.resize(n);
        st = sp_pack_tail_index(header.data(), header.size(), tail.data(), tail.size(), end, &info, p.index.data(), p.index.size(), &n);
    }
    if (st == SP_OK) { p.index.resize(n); p.len = end; }
    return st;
}

// The committed end of a pack whose last bytes are not a footer (an append in progress): the end of the last trailer
// that checks, walking the entries' 24-byte prefixes from the first page (docs/format.md, "Opening and torn tails").
sp_status recoverTail(Pack& p, const std::vector<uint8_t>& header, uint64_t size) {
    sp_status best = SP_ERR_NO_FOOTER;
    for (uint64_t at = SP_PACK_PAGE; at + 24 <= size;) {
        uint8_t pre[24];
        if (!p.read(at, pre, sizeof pre)) break;
        uint64_t len = 0;
        for (int i = 0; i < 8; ++i) len |= uint64_t(pre[8 + i]) << (8 * i);
        if (len < SP_PACK_PAGE || len % SP_PACK_PAGE || len > size - at) break;
        if (!std::memcmp(pre, "OSPI", 4)) {
            Pack q;  // a candidate committed pack ending with this trailer
#ifdef _WIN32
            q.file = p.file;
#else
            q.fd = p.fd;
#endif
            if (openTail(q, header, at, at + len) == SP_OK) { p.index = std::move(q.index); p.len = q.len; best = SP_OK; }
#ifdef _WIN32
            q.file = nullptr;
#else
            q.fd = -1;
#endif
        }
        at += len;
    }
    if (best != SP_OK && size >= SP_PACK_PAGE) {  // nothing committed past the header: an empty pack
        sp_pack_info info{};
        size_t n = 0;
        if (sp_pack_tail_index(header.data(), header.size(), nullptr, 0, SP_PACK_PAGE, &info, nullptr, 0, &n) == SP_OK) { p.len = SP_PACK_PAGE; p.index.clear(); best = SP_OK; }
    }
    return best;
}

std::mutex g_packsLock;
std::map<std::string, std::weak_ptr<const Pack>> g_packs;  // every pack open in this process, by path

// The pack at `path` covering byte `need` (opened again when the host has appended past what was committed).
std::shared_ptr<const Pack> packAt(const std::string& path, uint64_t need, std::string& error) {
    std::lock_guard<std::mutex> g(g_packsLock);
    auto& slot = g_packs[path];
    if (auto p = slot.lock(); p && p->len >= need) return p;
    auto p = std::make_shared<Pack>();
    p->path = path;
    uint64_t size = 0;
    if (!openFile(path, *p, size)) { error = "cannot open the pack " + path; return nullptr; }
    std::vector<uint8_t> header(SP_PACK_PAGE);
    if (size >= SP_PACK_PAGE && !p->read(0, header.data(), header.size())) { error = "cannot read the pack " + path; return nullptr; }
    sp_status st = size >= SP_PACK_PAGE ? SP_OK : SP_ERR_NOT_PACK;
    if (st == SP_OK && size > SP_PACK_PAGE) {
        uint8_t foot[kFooter] = {};
        uint64_t toff = 0, tlen = 0;
        if (size % SP_PACK_PAGE == 0 && p->read(size - kFooter, foot, kFooter) && !std::memcmp(foot, "OSPF", 4)) {
            for (int i = 0; i < 8; ++i) { toff |= uint64_t(foot[8 + i]) << (8 * i); tlen |= uint64_t(foot[16 + i]) << (8 * i); }
        }
        st = toff >= SP_PACK_PAGE && toff < size && tlen == size - toff ? openTail(*p, header, toff, size) : SP_ERR_NO_FOOTER;
        if (st == SP_ERR_NO_FOOTER || st == SP_ERR_BAD_FOOTER) st = recoverTail(*p, header, size);  // an append in progress
    } else if (st == SP_OK) {
        sp_pack_info info{};
        size_t n = 0;
        st = sp_pack_tail_index(header.data(), header.size(), nullptr, 0, size, &info, nullptr, 0, &n);
        p->len = size;
    }
    if (st != SP_OK) { error = "not a valid sample pack (error " + std::to_string(int(st)) + "): " + path; return nullptr; }
    slot = p;
    return p;
}

std::atomic<bool> g_failReads{false};
std::atomic<uint64_t> g_bytesRead{0};

uint32_t u32(const uint8_t* b) { return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; }

}  // namespace

Pack::~Pack() {
#ifdef _WIN32
    if (file) CloseHandle(file);
#else
    if (fd >= 0) close(fd);
#endif
}

void failReads(bool on) { g_failReads.store(on, std::memory_order_relaxed); }
uint64_t packBytesRead() { return g_bytesRead.load(std::memory_order_relaxed); }

bool Pack::read(uint64_t offset, void* out, size_t n) const {
    if (g_failReads.load(std::memory_order_relaxed)) return false;
    g_bytesRead.fetch_add(n, std::memory_order_relaxed);
    auto* b = static_cast<uint8_t*>(out);
#ifdef _WIN32
    while (n) {
        OVERLAPPED o{};
        o.Offset = DWORD(offset); o.OffsetHigh = DWORD(offset >> 32);
        DWORD got = 0;
        const DWORD ask = DWORD(std::min<size_t>(n, 1u << 30));
        if (!ReadFile(file, b, ask, &got, &o) || got == 0) return false;
        b += got; offset += got; n -= got;
    }
#else
    while (n) {
        const ssize_t got = pread(fd, b, n, off_t(offset));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return false;
        b += got; offset += uint64_t(got); n -= size_t(got);
    }
#endif
    return true;
}

bool sampleId(const char* ref, uint8_t id[SP_SAMPLE_ID_BYTES]) {
    if (std::strncmp(ref, "sample:", 7) || std::strlen(ref) != 7 + 2 * SP_SAMPLE_ID_BYTES) return false;
    for (int i = 0; i < 2 * SP_SAMPLE_ID_BYTES; ++i) {
        const char c = ref[7 + i];
        const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (v < 0) return false;
        id[i / 2] = uint8_t(i % 2 ? id[i / 2] | v : v << 4);
    }
    return true;
}

bool findRecord(const std::string& path, uint64_t offset, const uint8_t* id, uint64_t length, uint64_t preload, Record& r, std::string& error) {
    for (uint64_t need = 0;; need = offset + std::max<uint64_t>(length, SP_PACK_PAGE)) {  // opened again once when the host appended
        if (!(r.pack = packAt(path, need, error))) return false;
        if (length && preload) {
            r.entry = {}; std::memcpy(r.entry.id, id, SP_SAMPLE_ID_BYTES);
            r.entry.offset = offset; r.entry.length = length; r.entry.preload = preload;
            if (offset + length <= r.pack->len) return true;
        } else {
            size_t at = 0;
            if (sp_index_find(r.pack->index.data(), r.pack->index.size(), id, &at) == SP_OK && r.pack->index[at].offset == offset) { r.entry = r.pack->index[at]; return true; }
        }
        if (need) break;
    }
    error = "no record of that sample at that offset in " + path;
    return false;
}

std::shared_ptr<const Pack> openPack(const std::string& path, std::string& error) { return packAt(path, 0, error); }

bool onlyRecord(const std::string& path, Record& r, std::string& error) {
    if (!(r.pack = packAt(path, 0, error))) return false;
    if (r.pack->index.size() != 1) { error = path + " is not a one-sample pack"; return false; }
    r.entry = r.pack->index[0];
    return true;
}

bool checkRecord(Record& r, std::string& error) {
    if (r.entry.preload < SP_PACK_PAGE || r.entry.preload > r.entry.length || r.entry.offset + r.entry.length > r.pack->len) {
        error = "bad sample record (its entry lies outside the pack) in " + r.pack->path; return false;
    }
    // the header, credit and frame table (head length H at byte 24, a whole number of pages); the window is read with
    // the frames that need it
    std::vector<uint8_t> head(SP_PACK_PAGE);
    bool read = r.pack->read(r.entry.offset, head.data(), head.size());
    if (const uint32_t h = read ? u32(head.data() + 24) : 0; h > head.size() && h <= r.entry.preload) {
        head.resize(h);
        read = r.pack->read(r.entry.offset + SP_PACK_PAGE, head.data() + SP_PACK_PAGE, h - SP_PACK_PAGE);
    }
    if (!read) { error = "cannot read a sample record in " + r.pack->path; return false; }
    const sp_status st = sp_record_head(head.data(), head.size(), &r.entry, &r.info);
    if (st != SP_OK) { error = "bad sample record (error " + std::to_string(int(st)) + ") in " + r.pack->path; return false; }
    if (std::memcmp(r.info.id, r.entry.id, SP_SAMPLE_ID_BYTES)) { error = "the record in " + r.pack->path + " holds another sample"; return false; }
    if (r.info.max_frame > kMaxFrameBytes) { error = "a sample record in " + r.pack->path + " has a frame larger than the reader takes"; return false; }
    std::vector<uint32_t> t(size_t(r.info.blocks) + 1);
    for (size_t i = 0; i < t.size(); ++i) t[i] = u32(head.data() + r.info.table_offset + 4 * i);
    // one copy a record in the process, as the heads: the sounds that use the same record share it
    static std::mutex lock;
    static std::map<std::pair<std::string, uint64_t>, std::weak_ptr<const std::vector<uint32_t>>> tables;
    std::lock_guard<std::mutex> g(lock);
    for (auto it = tables.begin(); it != tables.end();) it = it->second.expired() ? tables.erase(it) : std::next(it);
    auto& slot = tables[{r.pack->path, r.entry.offset}];
    auto held = slot.lock();
    if (!held || *held != t) { held = std::make_shared<const std::vector<uint32_t>>(std::move(t)); slot = held; }
    r.table = std::move(held);
    return true;
}

int32_t decodeBlock(const Record& r, uint32_t block, Decoder& d) {
    if (!r.table || block >= r.info.blocks) return 0;
    const uint32_t a = (*r.table)[block], n = (*r.table)[block + 1] - a;
    if (n > kMaxFrameBytes) return 0;
    if (!r.pack->read(r.entry.offset + r.info.head_len + r.info.window_bytes + a, d.bytes, n)) return -1;
    sp_flac_frame_info fi{};
    if (sp_flac_decode_frame(d.bytes, n, 0, &d.scratch, d.ch[0], r.info.channels == 2 ? d.ch[1] : nullptr, SP_FLAC_MAX_BLOCK, &fi) != SP_OK) return 0;
    const uint32_t want = block + 1 < r.info.blocks ? r.info.block_size : r.info.frames - block * r.info.block_size;
    return fi.channels == r.info.channels && fi.samples == want && fi.consumed == n ? int32_t(want) : 0;
}

bool decodeFrames(const Record& r, uint32_t from, uint32_t count, int16_t* out, Decoder& d) {
    const uint32_t c = r.info.channels, b = r.info.block_size;
    for (uint32_t f = from; f < from + count;) {
        const uint32_t block = f / b, at = f - block * b;
        const int32_t got = decodeBlock(r, block, d);
        if (got < 0) return false;
        const uint32_t n = std::min(from + count - f, b - at);
        for (uint32_t k = 0; k < n; ++k)
            for (uint32_t ch = 0; ch < c; ++ch) out[(f - from + k) * c + ch] = int32_t(at + k) < got ? d.ch[ch][at + k] : 0;
        f += n;
    }
    return true;
}

void willNeed(const Pack& p, uint64_t offset, uint64_t length) {
    const uint64_t e = std::min<uint64_t>(offset + length, p.len);
    if (e <= offset) return;
#if defined(_WIN32)
    (void)p;  // shortcut: no read-ahead on Windows, not a device platform
#elif defined(__APPLE__)
    for (uint64_t a = offset; a < e;) {  // radvisory counts in int
        const uint64_t n = std::min<uint64_t>(e - a, 1u << 30);
        radvisory ra{off_t(a), int(n)};
        fcntl(p.fd, F_RDADVISE, &ra);
        a += n;
    }
#else
    posix_fadvise(p.fd, off_t(offset), off_t(e - offset), POSIX_FADV_WILLNEED);
#endif
}

}  // namespace pcmshell
