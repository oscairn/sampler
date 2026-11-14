// SPDX-License-Identifier: AGPL-3.0-or-later
#include "packs.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX  // windows.h would define min and max as macros
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace pcmshell {

namespace {

// Maps a file read-only; false when it cannot.
bool mapFile(const std::string& path, Pack& p) {
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring w(size_t(n > 0 ? n : 1), L'\0');
    if (n <= 0 || !MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], n)) return false;
    HANDLE f = CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    HANDLE m = GetFileSizeEx(f, &size) && size.QuadPart > 0 ? CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
    CloseHandle(f);
    if (!m) return false;
    p.base = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(m);  // the view keeps the mapping
    if (!p.base) return false;
    p.mapped = size_t(size.QuadPart);
#else
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st{};
    void* a = fstat(fd, &st) == 0 && st.st_size > 0 ? mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_SHARED, fd, 0) : MAP_FAILED;
    close(fd);  // the mapping keeps the file
    if (a == MAP_FAILED) return false;
    madvise(a, size_t(st.st_size), MADV_RANDOM);  // a fault reads its page, not megabytes around it
    p.base = a;
    p.mapped = size_t(st.st_size);
#endif
    p.data = static_cast<const uint8_t*>(p.base);
    return true;
}

std::mutex g_packsLock;
std::map<std::string, std::weak_ptr<const Pack>> g_packs;  // every pack mapped in this process, by path

// The pack at `path` covering byte `need` (mapped again when the host has appended past what is mapped).
std::shared_ptr<const Pack> mapPack(const std::string& path, uint64_t need, std::string& error) {
    std::lock_guard<std::mutex> g(g_packsLock);
    auto& slot = g_packs[path];
    if (auto p = slot.lock(); p && p->len >= need) return p;
    auto p = std::make_shared<Pack>();
    p->path = path;
    if (!mapFile(path, *p)) { error = "cannot open the pack " + path; return nullptr; }
    sp_pack_info info{};
    sp_status st = sp_pack_open(p->data, p->mapped, &info);
    uint64_t committed = info.length;
    if (st == SP_ERR_NO_FOOTER) {  // an append in progress: what was committed before it
        st = sp_pack_recover(p->data, p->mapped, &committed);
        if (st == SP_OK) st = sp_pack_open(p->data, size_t(committed), &info);
    }
    if (st != SP_OK) { error = "not a valid sample pack (error " + std::to_string(int(st)) + "): " + path; return nullptr; }
    p->len = size_t(committed);
    p->index.resize(size_t(info.records));
    size_t n = 0;
    if (info.records && sp_pack_index(p->data, p->len, p->index.data(), p->index.size(), &n) != SP_OK) { error = "cannot read the index of " + path; return nullptr; }
    slot = p;
    return p;
}

}  // namespace

Pack::~Pack() {
    if (!base) return;
#ifdef _WIN32
    UnmapViewOfFile(base);
#else
    munmap(base, mapped);
#endif
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
    for (uint64_t need = 0;; need = offset + std::max<uint64_t>(length, SP_PACK_PAGE)) {  // mapped again once when the host appended
        if (!(r.pack = mapPack(path, need, error))) return false;
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

std::shared_ptr<const Pack> openPack(const std::string& path, std::string& error) { return mapPack(path, 0, error); }

bool onlyRecord(const std::string& path, Record& r, std::string& error) {
    if (!(r.pack = mapPack(path, 0, error))) return false;
    if (r.pack->index.size() != 1) { error = path + " is not a one-sample pack"; return false; }
    r.entry = r.pack->index[0];
    return true;
}

bool checkRecord(Record& r, std::string& error) {
    const sp_status st = sp_pack_record(r.pack->data, r.pack->len, &r.entry, &r.info);
    if (st != SP_OK) { error = "bad sample record (error " + std::to_string(int(st)) + ") in " + r.pack->path; return false; }
    if (std::memcmp(r.info.id, r.entry.id, SP_SAMPLE_ID_BYTES)) { error = "the record in " + r.pack->path + " holds another sample"; return false; }
    return true;
}

uint32_t decodeBlock(const Record& r, uint32_t block, Decoder& d) {
    uint64_t n = 0;
    const sp_status st = sp_record_decode(r.pack->data, r.pack->len, &r.entry, block, 1, 0, &d.scratch, d.ch[0], r.info.channels == 2 ? d.ch[1] : nullptr,
                                          SP_FLAC_MAX_BLOCK, &n);
    return st == SP_OK ? uint32_t(n) : 0;
}

void decodeFrames(const Record& r, uint32_t from, uint32_t count, int16_t* out, Decoder& d) {
    const uint32_t c = r.info.channels, b = r.info.block_size;
    for (uint32_t f = from; f < from + count;) {
        const uint32_t block = f / b, at = f - block * b;
        const uint32_t got = decodeBlock(r, block, d);
        const uint32_t n = std::min(from + count - f, b - at);
        for (uint32_t k = 0; k < n; ++k)
            for (uint32_t ch = 0; ch < c; ++ch) out[(f - from + k) * c + ch] = at + k < got ? d.ch[ch][at + k] : 0;
        f += n;
    }
}

void willNeed(const Pack& p, uint64_t offset, uint64_t length) {
#ifdef _WIN32
    (void)p; (void)offset; (void)length;  // shortcut: no read-ahead on Windows (PrefetchVirtualMemory would do it), not a device platform
#else
    const uint64_t page = uint64_t(sysconf(_SC_PAGESIZE));
    const uint64_t a = offset / page * page, e = std::min<uint64_t>(offset + length, p.mapped);
    if (e > a) madvise(static_cast<char*>(p.base) + a, size_t(e - a), MADV_WILLNEED);
#endif
}

}  // namespace pcmshell
