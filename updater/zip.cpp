// Bounded ZIP reader + streaming inflate (RFC 1951). Supports stored and deflate entries, no ZIP64, no encryption.
#include "core.h"
#include <algorithm>
#include <cstring>
#include <set>

namespace rdu {
namespace {

constexpr uint64_t kMaxTotal = 3ull << 30;      // 3 GiB uncompressed
constexpr size_t kMaxEntries = 20000;
constexpr uint32_t kMaxCentralDir = 16u << 20;

uint32_t g_crcTable[256];
bool g_crcInit = [] {
    for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; g_crcTable[i] = c; }
    return true;
}();
uint32_t crcUpdate(uint32_t crc, const uint8_t* p, size_t n) {
    crc = ~crc; for (size_t i = 0; i < n; ++i) crc = g_crcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8); return ~crc;
}

struct File {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~File() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    bool openRead(const fs::path& p) { h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr); return h != INVALID_HANDLE_VALUE; }
    bool seek(uint64_t pos) { LARGE_INTEGER li; li.QuadPart = static_cast<LONGLONG>(pos); return SetFilePointerEx(h, li, nullptr, FILE_BEGIN) != 0; }
    bool readExact(void* buf, size_t n) { DWORD got = 0; return ReadFile(h, buf, static_cast<DWORD>(n), &got, nullptr) && got == n; }
    uint64_t size() const { LARGE_INTEGER li{}; GetFileSizeEx(h, &li); return static_cast<uint64_t>(li.QuadPart); }
};

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

// ---- inflate --------------------------------------------------------------------------------------------
struct InflateError { const char* msg; };
struct Huff { uint16_t count[16]; uint16_t symbol[288]; };

int buildHuff(Huff& h, const uint8_t* length, int n) {
    for (int l = 0; l < 16; ++l) h.count[l] = 0;
    for (int s = 0; s < n; ++s) h.count[length[s]]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int l = 1; l < 16; ++l) { left <<= 1; left -= h.count[l]; if (left < 0) return left; }
    uint16_t offs[16]; offs[1] = 0;
    for (int l = 1; l < 15; ++l) offs[l + 1] = static_cast<uint16_t>(offs[l] + h.count[l]);
    for (int s = 0; s < n; ++s) if (length[s]) h.symbol[offs[length[s]]++] = static_cast<uint16_t>(s);
    return left;
}

class Inflater {
public:
    Inflater(HANDLE in, uint64_t csize, HANDLE out, uint64_t usize, std::function<bool(uint64_t)> onBytes)
        : in_(in), inLeft_(csize), out_(out), usize_(usize), onBytes_(std::move(onBytes)), ibuf_(1 << 20), obuf_(1 << 20), win_(32768) {}
    // method 8 = deflate, 0 = stored. Returns false with err set on any malformed/oversized/cancelled stream.
    bool run(int method, uint32_t& crc, std::string& err) {
        try {
            if (method == 0) { const uint64_t n = inLeft0(); for (uint64_t i = 0; i < n; ++i) emit(nextByte()); }
            else inflate();
            flush();
            if (written_ != usize_) throw InflateError{"size differs from the archive directory"};
            crc = crc_; return true;
        } catch (const InflateError& e) { err = e.msg; return false; }
    }
private:
    uint64_t inLeft0() const { return inLeft_ + (ilen_ - ipos_); }
    uint8_t nextByte() {
        if (ipos_ == ilen_) {
            if (!inLeft_) throw InflateError{"compressed data ended early"};
            DWORD want = static_cast<DWORD>(std::min<uint64_t>(inLeft_, ibuf_.size())), got = 0;
            if (!ReadFile(in_, ibuf_.data(), want, &got, nullptr) || got == 0) throw InflateError{"read error in archive"};
            inLeft_ -= got; ipos_ = 0; ilen_ = got;
        }
        return ibuf_[ipos_++];
    }
    uint32_t bits(int n) {
        while (bitcnt_ < n) { bitbuf_ |= static_cast<uint64_t>(nextByte()) << bitcnt_; bitcnt_ += 8; }
        uint32_t v = static_cast<uint32_t>(bitbuf_ & ((1ull << n) - 1)); bitbuf_ >>= n; bitcnt_ -= n; return v;
    }
    void flush() {
        if (!olen_) return;
        DWORD wrote = 0;
        if (!WriteFile(out_, obuf_.data(), static_cast<DWORD>(olen_), &wrote, nullptr) || wrote != olen_) throw InflateError{"could not write extracted file (disk full?)"};
        crc_ = crcUpdate(crc_, obuf_.data(), olen_);
        size_t n = olen_; olen_ = 0;
        if (onBytes_ && !onBytes_(n)) throw InflateError{"cancelled"};
    }
    void emit(uint8_t b) {
        if (written_ >= usize_) throw InflateError{"data larger than the archive directory declares"};
        win_[written_ & 32767] = b; ++written_;
        obuf_[olen_++] = b; if (olen_ == obuf_.size()) flush();
    }
    int decode(const Huff& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            code |= static_cast<int>(bits(1));
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count; first += count; first <<= 1; code <<= 1;
        }
        throw InflateError{"invalid Huffman code"};
    }
    void codes(const Huff& lc, const Huff& dc) {
        static const uint16_t lens[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
        static const uint16_t lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
        static const uint16_t dists[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
        static const uint16_t dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
        for (;;) {
            int sym = decode(lc);
            if (sym < 256) emit(static_cast<uint8_t>(sym));
            else if (sym == 256) return;
            else {
                sym -= 257; if (sym >= 29) throw InflateError{"invalid length code"};
                uint32_t len = lens[sym] + bits(lext[sym]);
                int ds = decode(dc); if (ds >= 30) throw InflateError{"invalid distance code"};
                uint32_t dist = dists[ds] + bits(dext[ds]);
                if (dist > written_) throw InflateError{"distance too far back"};
                for (uint32_t i = 0; i < len; ++i) emit(win_[(written_ - dist) & 32767]);
            }
        }
    }
    void stored() {
        bits(bitcnt_ & 7);
        uint32_t len = bits(16), nlen = bits(16);
        if (len != (~nlen & 0xFFFF)) throw InflateError{"corrupt stored block"};
        for (uint32_t i = 0; i < len; ++i) emit(static_cast<uint8_t>(bits(8)));
    }
    void fixed() {
        static Huff lc, dc; static bool built = false;
        if (!built) {
            uint8_t l[288]; int s = 0;
            for (; s < 144; ++s) l[s] = 8; for (; s < 256; ++s) l[s] = 9; for (; s < 280; ++s) l[s] = 7; for (; s < 288; ++s) l[s] = 8;
            buildHuff(lc, l, 288);
            for (s = 0; s < 30; ++s) l[s] = 5;
            buildHuff(dc, l, 30); built = true;
        }
        codes(lc, dc);
    }
    void dynamic() {
        static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
        int nlen = static_cast<int>(bits(5)) + 257, ndist = static_cast<int>(bits(5)) + 1, ncode = static_cast<int>(bits(4)) + 4;
        if (nlen > 286 || ndist > 30) throw InflateError{"too many length or distance codes"};
        uint8_t lengths[320] = {};
        for (int i = 0; i < ncode; ++i) lengths[order[i]] = static_cast<uint8_t>(bits(3));
        Huff lc, dc;
        if (buildHuff(lc, lengths, 19) != 0) throw InflateError{"bad code-length code"};
        int idx = 0;
        while (idx < nlen + ndist) {
            int sym = decode(lc);
            if (sym < 16) lengths[idx++] = static_cast<uint8_t>(sym);
            else {
                int len = 0, rep;
                if (sym == 16) { if (!idx) throw InflateError{"repeat with no previous length"}; len = lengths[idx - 1]; rep = 3 + static_cast<int>(bits(2)); }
                else if (sym == 17) rep = 3 + static_cast<int>(bits(3));
                else rep = 11 + static_cast<int>(bits(7));
                if (idx + rep > nlen + ndist) throw InflateError{"too many lengths"};
                while (rep--) lengths[idx++] = static_cast<uint8_t>(len);
            }
        }
        if (lengths[256] == 0) throw InflateError{"missing end-of-block code"};
        int err = buildHuff(lc, lengths, nlen);
        if (err && (err < 0 || nlen != lc.count[0] + lc.count[1])) throw InflateError{"bad literal/length code"};
        err = buildHuff(dc, lengths + nlen, ndist);
        if (err && (err < 0 || ndist != dc.count[0] + dc.count[1])) throw InflateError{"bad distance code"};
        codes(lc, dc);
    }
    void inflate() {
        int last;
        do {
            last = static_cast<int>(bits(1));
            uint32_t type = bits(2);
            if (type == 0) stored(); else if (type == 1) fixed(); else if (type == 2) dynamic(); else throw InflateError{"invalid block type"};
        } while (!last);
    }
    HANDLE in_, out_; uint64_t inLeft_, usize_, written_ = 0; std::function<bool(uint64_t)> onBytes_;
    std::vector<uint8_t> ibuf_, obuf_, win_; size_t ipos_ = 0, ilen_ = 0, olen_ = 0;
    uint64_t bitbuf_ = 0; int bitcnt_ = 0; uint32_t crc_ = 0;
};

bool validUtf8(const std::string& s) {
    if (s.empty()) return true;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0) > 0;
}
std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c))); return s; }

}  // namespace

bool validateEntryName(const std::string& raw, std::string& out, std::wstring& why) {
    std::string n = raw;
    for (auto& c : n) if (c == '\\') c = '/';
    if (n.empty()) { why = L"empty entry name"; return false; }
    if (!validUtf8(n)) { why = L"entry name is not valid UTF-8"; return false; }
    if (n[0] == '/') { why = L"absolute path in archive: " + W(raw); return false; }
    if (n.size() > 240) { why = L"entry path too long"; return false; }
    bool dir = n.back() == '/';
    if (dir) n.pop_back();
    static const char* reserved[] = {"con","prn","aux","nul","com1","com2","com3","com4","com5","com6","com7","com8","com9","lpt1","lpt2","lpt3","lpt4","lpt5","lpt6","lpt7","lpt8","lpt9"};
    size_t pos = 0;
    while (true) {
        size_t slash = n.find('/', pos);
        std::string part = n.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        if (part.empty() || part == "." || part == "..") { why = L"unsafe path component in: " + W(raw); return false; }
        for (unsigned char c : part)
            if (c < 0x20 || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') { why = L"illegal character in entry name: " + W(raw); return false; }
        if (part.back() == '.' || part.back() == ' ') { why = L"entry name ends with a dot or space: " + W(raw); return false; }
        std::string stem = lower(part.substr(0, part.find('.')));
        for (auto r : reserved) if (stem == r) { why = L"reserved device name in: " + W(raw); return false; }
        if (slash == std::string::npos) break;
        pos = slash + 1;
    }
    out = n + (dir ? "/" : "");
    return true;
}

bool openZip(const fs::path& path, ZipPackage& pkg, std::wstring& err, bool requireManifest) {
    pkg = {}; pkg.file = path;
    File f;
    if (!f.openRead(path)) { err = L"Cannot open the downloaded package."; return false; }
    const uint64_t fsize = f.size();
    if (fsize < 22) { err = L"The package is not a valid ZIP file."; return false; }
    const size_t tail = static_cast<size_t>(std::min<uint64_t>(fsize, 22 + 65535));
    std::vector<uint8_t> t(tail);
    if (!f.seek(fsize - tail) || !f.readExact(t.data(), tail)) { err = L"Cannot read the package."; return false; }
    size_t eocd = std::string::npos;
    for (size_t i = tail - 22 + 1; i-- > 0;) if (rd32(&t[i]) == 0x06054b50 && i + 22 + rd16(&t[i + 20]) == tail) { eocd = i; break; }
    if (eocd == std::string::npos) { err = L"The package is not a valid ZIP file (no end record)."; return false; }
    const uint8_t* e = &t[eocd];
    uint32_t count = rd16(e + 10), cdSize = rd32(e + 12), cdOff = rd32(e + 16);
    if (rd16(e + 4) || rd16(e + 6) || rd16(e + 8) != count) { err = L"Multi-disk ZIP files are not supported."; return false; }
    if (count == 0xFFFF || cdSize == 0xFFFFFFFF || cdOff == 0xFFFFFFFF) { err = L"ZIP64 packages are not supported."; return false; }
    if (count == 0 || count > kMaxEntries || cdSize > kMaxCentralDir || static_cast<uint64_t>(cdOff) + cdSize > fsize) { err = L"The ZIP directory is invalid or too large."; return false; }
    std::vector<uint8_t> cd(cdSize);
    if (!f.seek(cdOff) || !f.readExact(cd.data(), cdSize)) { err = L"Cannot read the ZIP directory."; return false; }

    struct Raw { std::string name; ZipEntry e; };
    std::vector<Raw> raws; size_t p = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (p + 46 > cd.size() || rd32(&cd[p]) != 0x02014b50) { err = L"The ZIP directory is corrupt."; return false; }
        const uint8_t* c = &cd[p];
        uint16_t madeBy = rd16(c + 4), flags = rd16(c + 8), method = rd16(c + 10), nlen = rd16(c + 28), xlen = rd16(c + 30), clen = rd16(c + 32);
        uint32_t extAttr = rd32(c + 38), localOff = rd32(c + 42);
        if (p + 46 + nlen + xlen + clen > cd.size()) { err = L"The ZIP directory is corrupt."; return false; }
        Raw r; r.name.assign(reinterpret_cast<const char*>(c + 46), nlen);
        r.e.crc = rd32(c + 16); r.e.csize = rd32(c + 20); r.e.usize = rd32(c + 24); r.e.method = method;
        p += 46 + nlen + xlen + clen;
        if (flags & 0x41) { err = L"Encrypted ZIP entries are not allowed."; return false; }
        if (method != 0 && method != 8) { err = L"Unsupported ZIP compression method."; return false; }
        if (r.e.csize == 0xFFFFFFFF || r.e.usize == 0xFFFFFFFF || localOff == 0xFFFFFFFF) { err = L"ZIP64 entries are not supported."; return false; }
        if ((madeBy >> 8) == 3 && ((extAttr >> 16) & 0xF000) == 0xA000) { err = L"Symbolic links in the package are not allowed."; return false; }
        std::wstring why;
        if (!validateEntryName(r.name, r.e.name, why)) { err = L"Unsafe package rejected: " + why; return false; }
        r.e.isDir = r.e.name.back() == '/';
        if (r.e.isDir && (r.e.usize || r.e.csize)) { err = L"A directory entry has data."; return false; }
        // Local header must agree with the directory (guards against header/data confusion).
        uint8_t lh[30];
        if (!f.seek(localOff) || !f.readExact(lh, 30) || rd32(lh) != 0x04034b50) { err = L"A ZIP entry header is corrupt."; return false; }
        uint16_t lnlen = rd16(lh + 26), lxlen = rd16(lh + 28);
        std::string lname(lnlen, '\0');
        if (lnlen != nlen || !f.readExact(lname.data(), lnlen) || lname != r.name) { err = L"A ZIP entry name does not match its header."; return false; }
        r.e.dataOffset = static_cast<uint64_t>(localOff) + 30 + lnlen + lxlen;
        if (r.e.dataOffset + r.e.csize > cdOff) { err = L"A ZIP entry extends past the directory (truncated or corrupt)."; return false; }
        if (r.e.method == 0 && r.e.csize != r.e.usize) { err = L"A stored entry has inconsistent sizes."; return false; }
        pkg.totalSize += r.e.usize;
        if (pkg.totalSize > kMaxTotal) { err = L"The package expands to more than 3 GiB."; return false; }
        raws.push_back(std::move(r));
    }

    // Strip a single container folder (public ZIPs use "ReDot-v1.0.0/") and require ReDot.exe at the install root.
    auto hasAt = [&](const std::string& prefix, const char* file) {
        for (auto& r : raws) if (lower(r.e.name) == lower(prefix + file)) return true; return false; };
    std::string prefix;
    if (!hasAt("", "ReDot.exe")) {
        std::string first = raws[0].e.name.substr(0, raws[0].e.name.find('/'));
        prefix = first + "/";
        if (first.empty() || !hasAt(prefix, "ReDot.exe")) { err = L"The package does not contain ReDot.exe."; return false; }
        for (auto& r : raws) if (r.e.name.compare(0, prefix.size(), prefix) != 0) { err = L"The package has files outside its single top-level folder."; return false; }
    }
    pkg.strippedRoot = prefix;
    const bool haveManifest = hasAt(prefix, "redot-version.json");   // before names are rewritten relative to the root
    std::set<std::string> seen;
    static const char* userData[] = {"settings-v1.txt", "scenes-v1.json", "workspace-layout-v1.txt"};
    for (auto& r : raws) {
        std::string rel = r.e.name.substr(prefix.size());
        if (rel.empty()) continue;                                // the container folder entry itself
        std::string l = lower(rel);
        if (l.compare(0, 13, ".redot-update") == 0 || l.compare(0, 9, "pandaclip") == 0) { err = L"The package contains a reserved or user-data path: " + W(rel); return false; }
        std::string leaf = l.substr(l.find_last_of('/') == std::string::npos ? 0 : l.find_last_of('/') + 1);
        for (auto u : userData) if (leaf == u) { err = L"The package contains a user-data file: " + W(rel); return false; }
        std::string key = l; if (!key.empty() && key.back() == '/') key.pop_back();
        if (!seen.insert(key).second) { err = L"The package contains duplicate entries: " + W(rel); return false; }
        r.e.name = rel; pkg.entries.push_back(r.e);
    }
    // A file path must not also be a folder of another entry.
    for (auto& a : pkg.entries) if (!a.isDir) for (auto& b : pkg.entries)
        if (b.name.size() > a.name.size() && lower(b.name).compare(0, a.name.size() + 1, lower(a.name) + "/") == 0) { err = L"The package has a file and folder with the same name."; return false; }
    if (requireManifest && !haveManifest) { err = L"The package has no redot-version.json manifest."; return false; }
    return true;
}

bool extractZip(const ZipPackage& pkg, const fs::path& staging, const ProgressFn& progress, std::wstring& err) {
    std::error_code ec;
    fs::create_directories(staging, ec);
    if (ec || !fs::is_empty(staging, ec)) { err = L"The staging folder is not empty."; return false; }
    const fs::path root = fs::weakly_canonical(staging, ec);
    File zip;
    if (!zip.openRead(pkg.file)) { err = L"Cannot reopen the package."; return false; }
    uint64_t done = 0;
    for (const auto& e : pkg.entries) {
        fs::path dest = (root / fs::path(W(e.name))).lexically_normal();
        std::wstring rs = root.wstring() + L"\\";
        if (_wcsnicmp(dest.c_str(), rs.c_str(), rs.size()) != 0) { err = L"Entry resolves outside the staging folder: " + W(e.name); return false; }
        if (e.isDir) { fs::create_directories(dest, ec); continue; }
        fs::create_directories(dest.parent_path(), ec);
        HANDLE out = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE) { err = L"Cannot create staged file: " + W(e.name); return false; }
        if (!zip.seek(e.dataOffset)) { CloseHandle(out); err = L"Cannot read package data."; return false; }
        uint32_t crc = 0; std::string ierr;
        Inflater inf(zip.h, e.csize, out, e.usize, [&](uint64_t n) { done += n; return !progress || progress(done, pkg.totalSize); });
        bool ok = inf.run(e.method, crc, ierr);
        CloseHandle(out);
        if (!ok && ierr == "cancelled") { err = L"Cancelled."; return false; }
        if (!ok) { err = L"Package entry " + W(e.name) + L" is corrupt: " + W(ierr); return false; }
        if (crc != e.crc) { err = L"Package entry " + W(e.name) + L" failed its CRC check."; return false; }
    }
    return true;
}

}  // namespace rdu
