#include "core.h"
#include <bcrypt.h>
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <system_error>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "version.lib")

namespace rdu {

std::wstring W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string U8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// ---- versions -------------------------------------------------------------------------------------------
bool parseVersion(std::string t, Version& out) {
    size_t b = t.find_first_not_of(" \t\r\n"), e = t.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    t = t.substr(b, e - b + 1);
    if (!t.empty() && (t[0] == 'v' || t[0] == 'V')) t.erase(0, 1);
    if (size_t plus = t.find('+'); plus != std::string::npos) t.erase(plus);
    std::string pre;
    if (size_t dash = t.find('-'); dash != std::string::npos) {
        pre = t.substr(dash + 1); t.erase(dash);
        if (pre.empty()) return false;
        for (char c : pre) if (!isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') return false;
    }
    int parts[3]; size_t pos = 0;
    for (int i = 0; i < 3; ++i) {
        size_t dot = t.find('.', pos);
        if (i < 2 && dot == std::string::npos) return false;
        std::string seg = t.substr(pos, i < 2 ? dot - pos : std::string::npos);
        if (seg.empty() || seg.size() > 9) return false;
        for (char c : seg) if (!isdigit(static_cast<unsigned char>(c))) return false;
        parts[i] = std::stoi(seg);
        if (i < 2) pos = dot + 1;
    }
    out.major = parts[0]; out.minor = parts[1]; out.patch = parts[2]; out.pre = pre;
    return true;
}
static std::vector<std::string> splitDots(const std::string& s) {
    std::vector<std::string> r; size_t p = 0;
    while (true) { size_t d = s.find('.', p); r.push_back(s.substr(p, d == std::string::npos ? d : d - p)); if (d == std::string::npos) break; p = d + 1; }
    return r;
}
static bool allDigits(const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return isdigit(static_cast<unsigned char>(c)) != 0; }); }
int compareVersions(const Version& a, const Version& b) {
    auto cmp = [](int x, int y) { return x < y ? -1 : x > y ? 1 : 0; };
    if (int c = cmp(a.major, b.major)) return c;
    if (int c = cmp(a.minor, b.minor)) return c;
    if (int c = cmp(a.patch, b.patch)) return c;
    if (a.pre.empty() && b.pre.empty()) return 0;
    if (a.pre.empty()) return 1;
    if (b.pre.empty()) return -1;
    auto pa = splitDots(a.pre), pb = splitDots(b.pre);
    for (size_t i = 0; i < std::min(pa.size(), pb.size()); ++i) {
        bool na = allDigits(pa[i]), nb = allDigits(pb[i]);
        if (na && nb) { unsigned long long x = std::stoull(pa[i]), y = std::stoull(pb[i]); if (x != y) return x < y ? -1 : 1; }
        else if (na != nb) return na ? -1 : 1;
        else if (int c = pa[i].compare(pb[i])) return c < 0 ? -1 : 1;
    }
    return pa.size() == pb.size() ? 0 : pa.size() < pb.size() ? -1 : 1;
}
std::string versionString(const Version& v) {
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch) + (v.pre.empty() ? "" : "-" + v.pre);
}

// ---- JSON -----------------------------------------------------------------------------------------------
const Json* Json::get(const char* key) const {
    if (type != Obj) return nullptr;
    for (auto& p : o) if (p.first == key) return &p.second;
    return nullptr;
}
std::string Json::str(const char* key) const { auto* p = get(key); return p && p->type == Str ? p->s : std::string(); }
bool Json::flag(const char* key) const { auto* p = get(key); return p && p->type == Bool && p->b; }
double Json::num(const char* key, double fb) const { auto* p = get(key); return p && p->type == Num ? p->n : fb; }

namespace {
class JsonParser {
public:
    JsonParser(const std::string& s) : s_(s) {}
    bool parse(Json& out, std::string& err) {
        ws();
        if (!value(out) ) { err = err_; return false; }
        ws();
        if (i_ != s_.size()) { err = "trailing characters in JSON"; return false; }
        return true;
    }
private:
    bool fail(const char* m) { err_ = m; return false; }
    void ws() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\r' || s_[i_] == '\n')) ++i_; }
    bool lit(const char* w) { size_t n = strlen(w); if (s_.compare(i_, n, w) == 0) { i_ += n; return true; } return false; }
    static void putUtf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    bool hex4(unsigned& v) {
        if (i_ + 4 > s_.size()) return false; v = 0;
        for (int k = 0; k < 4; ++k) { char c = s_[i_++]; v <<= 4; if (c >= '0' && c <= '9') v |= c - '0'; else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10; else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10; else return false; }
        return true;
    }
    bool string(std::string& o) {
        ++i_;  // opening quote
        while (i_ < s_.size()) {
            char c = s_[i_++];
            if (c == '"') return true;
            if (c != '\\') { o += c; continue; }
            if (i_ >= s_.size()) break;
            char e = s_[i_++];
            switch (e) {
            case '"': o += '"'; break; case '\\': o += '\\'; break; case '/': o += '/'; break;
            case 'b': o += '\b'; break; case 'f': o += '\f'; break; case 'n': o += '\n'; break; case 'r': o += '\r'; break; case 't': o += '\t'; break;
            case 'u': {
                unsigned cp; if (!hex4(cp)) return fail("bad \\u escape");
                if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(i_, 2, "\\u") == 0) { i_ += 2; unsigned lo; if (!hex4(lo)) return fail("bad \\u escape"); cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
                putUtf8(o, cp); break; }
            default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool value(Json& o) {
        if (++depth_ > 64) return fail("JSON nested too deeply");
        bool ok = value2(o); --depth_; return ok;
    }
    bool value2(Json& o) {
        if (i_ >= s_.size()) return fail("unexpected end of JSON");
        char c = s_[i_];
        if (c == '{') {
            o.type = Json::Obj; ++i_; ws();
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
            while (true) {
                ws(); if (i_ >= s_.size() || s_[i_] != '"') return fail("expected object key");
                std::string k; if (!string(k)) return false;
                ws(); if (i_ >= s_.size() || s_[i_] != ':') return fail("expected ':'");
                ++i_; ws(); Json v; if (!value(v)) return false;
                o.o.emplace_back(std::move(k), std::move(v));
                ws(); if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            o.type = Json::Arr; ++i_; ws();
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
            while (true) {
                ws(); Json v; if (!value(v)) return false; o.a.push_back(std::move(v));
                ws(); if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { o.type = Json::Str; return string(o.s); }
        if (lit("true")) { o.type = Json::Bool; o.b = true; return true; }
        if (lit("false")) { o.type = Json::Bool; o.b = false; return true; }
        if (lit("null")) { o.type = Json::Null; return true; }
        char* end = nullptr; const char* st = s_.c_str() + i_;
        double d = strtod(st, &end);
        if (end == st) return fail("unexpected character in JSON");
        o.type = Json::Num; o.n = d; i_ += static_cast<size_t>(end - st); return true;
    }
    const std::string& s_; size_t i_ = 0; int depth_ = 0; std::string err_;
};
}  // namespace

bool parseJson(const std::string& text, Json& out, std::string& err) { out = {}; return JsonParser(text).parse(out, err); }

// ---- releases -------------------------------------------------------------------------------------------
FeedResult selectLatestRelease(const std::string& body) {
    FeedResult r; Json j; std::string err;
    if (!parseJson(body, j, err) || j.type != Json::Arr) { r.status = FeedStatus::BadFeed; r.detail = err.empty() ? "unexpected GitHub response" : err; return r; }
    const Json* best = nullptr; Version bestV;
    for (auto& rel : j.a) {
        if (rel.type != Json::Obj || rel.flag("draft") || rel.flag("prerelease")) continue;
        Version v;
        if (!parseVersion(rel.str("tag_name"), v) || !v.pre.empty()) continue;
        if (!best || compareVersions(v, bestV) > 0) { best = &rel; bestV = v; }
    }
    if (!best) { r.status = FeedStatus::NoStableRelease; return r; }
    r.release.tag = best->str("tag_name"); r.release.version = bestV; r.release.notes = best->str("body");
    const std::string zipName = "ReDot-v" + versionString(bestV) + "-Windows-x64.zip", sigName = zipName + ".sig";
    if (auto* assets = best->get("assets"); assets && assets->type == Json::Arr)
        for (auto& a : assets->a) {
            Asset as; as.name = a.str("name"); as.url = a.str("browser_download_url"); as.size = static_cast<uint64_t>(a.num("size"));
            if (as.url.compare(0, strlen(kReleaseUrlPrefix), kReleaseUrlPrefix) != 0) continue;   // official repo only
            if (as.name == zipName) r.release.zip = as; else if (as.name == sigName) r.release.sig = as;
        }
    if (r.release.zip.url.empty() || r.release.sig.url.empty()) {
        r.status = FeedStatus::MissingAssets;
        r.detail = "Release " + r.release.tag + " has no official " + zipName + (r.release.zip.url.empty() ? "" : " signature") + " asset.";
        return r;
    }
    r.status = FeedStatus::Ok; return r;
}

// ---- crypto ---------------------------------------------------------------------------------------------
bool sha256File(const fs::path& file, std::vector<uint8_t>& digest, const ProgressFn& progress, std::wstring& err) {
    HANDLE f = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) { err = L"Cannot open file for hashing."; return false; }
    LARGE_INTEGER sz{}; GetFileSizeEx(f, &sz);
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE h = nullptr; bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 && BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<uint8_t> buf(1 << 20); uint64_t done = 0; ok = true;
        for (;;) {
            DWORD got = 0;
            if (!ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) { ok = false; err = L"Read error while hashing."; break; }
            if (!got) break;
            BCryptHashData(h, buf.data(), got, 0); done += got;
            if (progress && !progress(done, static_cast<uint64_t>(sz.QuadPart))) { ok = false; err = L"Cancelled."; break; }
        }
        if (ok) { digest.assign(32, 0); ok = BCryptFinishHash(h, digest.data(), 32, 0) == 0; if (!ok) err = L"Hash failed."; }
    } else err = L"Windows cryptography is unavailable.";
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return ok;
}
bool verifyP256(const uint8_t* pub, size_t pubSize, const std::vector<uint8_t>& digest, const std::vector<uint8_t>& sig) {
    if (!pub || digest.size() != 32 || sig.size() != 64) return false;
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_KEY_HANDLE key = nullptr; bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, const_cast<PUCHAR>(pub), static_cast<ULONG>(pubSize), 0) == 0)
        ok = BCryptVerifySignature(key, nullptr, const_cast<PUCHAR>(digest.data()), 32, const_cast<PUCHAR>(sig.data()), 64, 0) == 0;
    if (key) BCryptDestroyKey(key);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}
bool generateP256(std::vector<uint8_t>& priv, std::vector<uint8_t>& pub) {
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_KEY_HANDLE key = nullptr; bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) == 0 && BCryptGenerateKeyPair(alg, &key, 256, 0) == 0 && BCryptFinalizeKeyPair(key, 0) == 0) {
        ULONG n1 = 0, n2 = 0;
        if (BCryptExportKey(key, nullptr, BCRYPT_ECCPRIVATE_BLOB, nullptr, 0, &n1, 0) == 0 && BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &n2, 0) == 0) {
            priv.resize(n1); pub.resize(n2);
            ok = BCryptExportKey(key, nullptr, BCRYPT_ECCPRIVATE_BLOB, priv.data(), n1, &n1, 0) == 0 && BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, pub.data(), n2, &n2, 0) == 0;
        }
    }
    if (key) BCryptDestroyKey(key);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}
bool signDigest(const std::vector<uint8_t>& priv, const std::vector<uint8_t>& digest, std::vector<uint8_t>& sig) {
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_KEY_HANDLE key = nullptr; bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPRIVATE_BLOB, &key, const_cast<PUCHAR>(priv.data()), static_cast<ULONG>(priv.size()), 0) == 0) {
        sig.assign(64, 0); ULONG n = 0;
        ok = BCryptSignHash(key, nullptr, const_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size()), sig.data(), 64, &n, 0) == 0 && n == 64;
    }
    if (key) BCryptDestroyKey(key);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

// ---- identification -------------------------------------------------------------------------------------
static std::string readSmallFile(const fs::path& p, size_t cap = 1 << 20) {
    std::ifstream f(p, std::ios::binary); std::string s;
    if (!f) return s;
    s.resize(cap); f.read(s.data(), static_cast<std::streamsize>(cap)); s.resize(static_cast<size_t>(f.gcount()));
    return s;
}
static bool exeFileVersion(const fs::path& exe, Version& v) {
    DWORD h = 0, sz = GetFileVersionInfoSizeW(exe.c_str(), &h);
    if (!sz) return false;
    std::vector<char> buf(sz);
    if (!GetFileVersionInfoW(exe.c_str(), 0, sz, buf.data())) return false;
    VS_FIXEDFILEINFO* fi = nullptr; UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<void**>(&fi), &len) || !fi) return false;
    v.major = HIWORD(fi->dwFileVersionMS); v.minor = LOWORD(fi->dwFileVersionMS); v.patch = HIWORD(fi->dwFileVersionLS); v.pre.clear();
    return true;
}
IdentStatus identifyInstall(const fs::path& dir, InstallInfo& info, std::wstring& why) {
    std::error_code ec;
    if (!fs::is_regular_file(dir / L"ReDot.exe", ec)) { why = L"There is no ReDot.exe in that folder."; return IdentStatus::NotReDot; }
    info = {}; info.dir = dir;
    if (fs::is_regular_file(dir / L"redot-version.json", ec)) {
        Json j; std::string err;
        if (!parseJson(readSmallFile(dir / L"redot-version.json"), j, err) || j.str("product") != "ReDot" || !parseVersion(j.str("version"), info.version)) {
            why = L"redot-version.json is unreadable or not a ReDot manifest."; return IdentStatus::Unrecognized;
        }
        return IdentStatus::Ok;
    }
    Version ev;
    if (exeFileVersion(dir / L"ReDot.exe", ev) && ev.major == 0 && ev.minor == 3 && ev.patch == 0 && fs::is_regular_file(dir / L"THIRD-PARTY-NOTICES.txt", ec)) {
        // The public v1.0.0 ZIP shipped an executable whose embedded version is still 0.3.0.
        info.version = {1, 0, 0, {}}; info.legacyInferred = true; return IdentStatus::Ok;
    }
    why = L"This ReDot has no redot-version.json and is not the public v1.0.0 package, so it looks like a development build. The updater will not modify it.";
    return IdentStatus::Unrecognized;
}

static std::wstring lowerPath(const fs::path& p) {
    std::error_code ec; std::wstring s = fs::weakly_canonical(p, ec).wstring();
    if (ec) s = p.wstring();
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}
static bool nameHasRedot(const fs::path& p) {
    std::wstring n = p.filename().wstring(); for (auto& c : n) c = static_cast<wchar_t>(towlower(c));
    return n.find(L"redot") != std::wstring::npos || n.find(L"pandaclip") != std::wstring::npos;
}
std::vector<fs::path> findInstallations(const std::vector<fs::path>& roots) {
    std::vector<fs::path> found; std::set<std::wstring> seen; size_t visited = 0;
    auto consider = [&](const fs::path& d) {
        InstallInfo info; std::wstring why;
        if (identifyInstall(d, info, why) != IdentStatus::Ok) return;
        if (seen.insert(lowerPath(d)).second) found.push_back(d);
    };
    for (auto& root : roots) {
        std::error_code ec;
        if (root.empty() || !fs::is_directory(root, ec)) continue;
        consider(root);
        for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end && visited < 4000; it.increment(ec)) {
            std::error_code e2;
            if (!it->is_directory(e2) || !nameHasRedot(it->path())) continue;
            ++visited; consider(it->path());
            for (fs::directory_iterator it2(it->path(), fs::directory_options::skip_permission_denied, e2), end2; !e2 && it2 != end2 && visited < 4000; it2.increment(e2)) {
                std::error_code e3;
                if (it2->is_directory(e3)) { ++visited; consider(it2->path()); }
            }
        }
    }
    return found;
}

static bool forEachReDotProcess(const std::function<bool(const fs::path&)>& fn) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{sizeof(pe)};
    bool stop = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !stop; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"ReDot.exe") != 0) continue;
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!p) continue;
        wchar_t path[32768]; DWORD n = 32768;
        if (QueryFullProcessImageNameW(p, 0, path, &n)) stop = fn(fs::path(path));
        CloseHandle(p);
    }
    CloseHandle(snap);
    return stop;
}
bool isReDotRunning(const fs::path& exe) {
    const std::wstring want = lowerPath(exe);
    return forEachReDotProcess([&](const fs::path& p) { return lowerPath(p) == want; });
}
std::vector<fs::path> runningReDotFolders() {
    std::vector<fs::path> r;
    forEachReDotProcess([&](const fs::path& p) { r.push_back(p.parent_path()); return false; });
    return r;
}

// ---- install / rollback ---------------------------------------------------------------------------------
namespace {
struct Journal {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~Journal() { close(); }
    void close() { if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); h = INVALID_HANDLE_VALUE; } }
    bool open(const fs::path& p) { h = CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr); return h != INVALID_HANDLE_VALUE; }
    bool line(const std::string& s) { std::string t = s + "\n"; DWORD w = 0; return WriteFile(h, t.data(), static_cast<DWORD>(t.size()), &w, nullptr) && w == t.size() && FlushFileBuffers(h); }
};
fs::path workDir(const fs::path& install) { return install / kWorkDirName; }
fs::path relPath(const std::string& rel) { return fs::path(W(rel)); }

bool rollbackJournal(const fs::path& install, std::wstring& detail) {
    const fs::path jpath = workDir(install) / L"journal.txt";
    std::ifstream in(jpath, std::ios::binary);
    std::string line, backupName; std::vector<std::string> order, mkdirs; std::set<std::string> placing, seenRel;
    bool done = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto sp = line.find(' '); std::string cmd = line.substr(0, sp), arg = sp == std::string::npos ? "" : line.substr(sp + 1);
        if (cmd == "BACKUPDIR") backupName = arg;
        else if (cmd == "BACKING" || cmd == "PLACING") { if (seenRel.insert(arg).second) order.push_back(arg); if (cmd == "PLACING") placing.insert(arg); }
        else if (cmd == "MKDIR") mkdirs.push_back(arg);
        else if (cmd == "DONE") done = true;
    }
    in.close();
    if (done) { std::error_code ec; fs::remove(jpath, ec); return true; }
    const fs::path backup = workDir(install) / W(backupName);
    std::vector<std::wstring> failures;
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        std::error_code ec; fs::path dest = install / relPath(*it), bak = backup / relPath(*it);
        if (!backupName.empty() && fs::exists(bak, ec)) {
            if (fs::exists(dest, ec)) fs::remove(dest, ec);
            fs::create_directories(dest.parent_path(), ec);
            if (!MoveFileExW(bak.c_str(), dest.c_str(), 0)) failures.push_back(W(*it));
        } else if (placing.count(*it) && fs::exists(dest, ec)) {
            fs::remove(dest, ec);                                // a file that did not exist before the update
            if (ec) failures.push_back(W(*it));
        }
    }
    for (auto it = mkdirs.rbegin(); it != mkdirs.rend(); ++it) RemoveDirectoryW((install / relPath(*it)).c_str());   // only removes empty folders
    if (!failures.empty()) {
        detail = L"Could not restore: " + failures[0] + (failures.size() > 1 ? L" and others" : L"");
        return false;
    }
    std::error_code ec;
    if (!backupName.empty()) fs::remove_all(backup, ec);       // everything was moved back; only empty folders remain
    fs::remove(jpath, ec);
    return true;
}
}  // namespace

RecoverResult recoverInterrupted(const fs::path& install, std::wstring& message) {
    std::error_code ec;
    if (!fs::exists(workDir(install) / L"journal.txt", ec)) return RecoverResult::Nothing;
    std::wstring detail;
    bool done = [&] { std::ifstream in(workDir(install) / L"journal.txt", std::ios::binary); std::string l; while (std::getline(in, l)) if (l.compare(0, 4, "DONE") == 0) return true; return false; }();
    if (rollbackJournal(install, detail)) {
        if (done) return RecoverResult::Nothing;
        message = L"A previous update was interrupted. The previous version has been restored.";
        return RecoverResult::RolledBack;
    }
    message = L"A previous update was interrupted and could not be fully undone (" + detail + L"). Your files are in " + (workDir(install)).wstring() + L".";
    return RecoverResult::Failed;
}

bool installStaged(const InstallOptions& o, const std::vector<std::string>& relsIn, std::wstring& err) {
    std::error_code ec;
    const fs::path work = workDir(o.installDir);
    fs::create_directories(work, ec);
    // ReDot.exe and the manifest go last so a half-finished update never looks complete.
    std::vector<std::string> rels = relsIn;
    auto moveLast = [&](const char* name) { auto it = std::find_if(rels.begin(), rels.end(), [&](const std::string& r) { std::string l = r; for (auto& c : l) c = static_cast<char>(tolower(static_cast<unsigned char>(c))); return l == name; }); if (it != rels.end()) { std::string v = *it; rels.erase(it); rels.push_back(v); } };
    moveLast("redot.exe"); moveLast("redot-version.json");

    const std::string backupName = "backup-" + (o.oldVersion.empty() ? std::string("old") : o.oldVersion) + "-" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    const fs::path backup = work / W(backupName);
    Journal j;
    if (!j.open(work / L"journal.txt")) { err = L"Cannot create the update journal (is the folder writable?)."; return false; }
    fs::create_directories(backup, ec);
    j.line("BACKUPDIR " + backupName);

    auto fail = [&](const std::wstring& why) {
        std::wstring detail;
        j.close();                                              // release the journal before rolling back
        bool restored = rollbackJournal(o.installDir, detail);
        err = why + (restored ? L" The previous version was restored." : L" Restoring the previous version also failed (" + detail + L"); your original files are in " + backup.wstring() + L".");
        return false;
    };
    for (size_t i = 0; i < rels.size(); ++i) {
        const std::string& rel = rels[i];
        fs::path dest = o.installDir / relPath(rel), staged = o.stagingDir / relPath(rel);
        // Create missing parent folders, remembering them so a rollback can remove the empty ones.
        std::vector<fs::path> missing;
        for (fs::path d = dest.parent_path(); d != o.installDir && !fs::exists(d, ec); d = d.parent_path()) missing.push_back(d);
        for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
            std::string r = U8(fs::relative(*it, o.installDir, ec).generic_wstring());
            j.line("MKDIR " + r); fs::create_directory(*it, ec);
        }
        if (fs::exists(dest, ec)) {
            if (fs::is_directory(dest, ec)) return fail(L"A folder is in the way of " + W(rel) + L".");
            j.line("BACKING " + rel);
            fs::path bak = backup / relPath(rel); fs::create_directories(bak.parent_path(), ec);
            if (!MoveFileExW(dest.c_str(), bak.c_str(), 0)) return fail(L"Could not replace " + W(rel) + L" (is ReDot or another program still using it?).");
            j.line("BACKED " + rel);
        }
        j.line("PLACING " + rel);
        if (!MoveFileExW(staged.c_str(), dest.c_str(), 0)) return fail(L"Could not install " + W(rel) + L".");
        j.line("PLACED " + rel);
        if (o.crashAtIndex == i) { err = L"Simulated crash."; return false; }
        if (o.failHook && o.failHook(i, rel)) return fail(L"Simulated installation failure.");
    }
    j.line("DONE");
    j.close();
    // Keep only the newest backup; older ones are the updater's own files.
    for (fs::directory_iterator it(work, ec), end; !ec && it != end; it.increment(ec)) {
        std::wstring n = it->path().filename().wstring();
        if (n.rfind(L"backup-", 0) == 0 && it->path() != backup) { std::error_code e2; fs::remove_all(it->path(), e2); }
    }
    fs::remove(work / L"journal.txt", ec);
    return true;
}

// ---- orchestration --------------------------------------------------------------------------------------
namespace {
struct Stop { Outcome outcome; std::wstring message, recovery; };

std::wstring fmtMB(uint64_t b) { wchar_t t[32]; swprintf_s(t, L"%.0f MB", static_cast<double>(b) / 1048576.0); return t; }
}  // namespace

RunResult runUpdate(const Options& o, INet& net, Reporter& rep) {
    RunResult res;
    const fs::path install = o.installDir, work = workDir(install);
    auto cancelled = [&] { return o.cancel && o.cancel->load(); };
    try {
        std::error_code ec;
        rep.status(L"Checking your ReDot installation...");
        std::wstring msg;
        auto rec = recoverInterrupted(install, msg);
        if (rec == RecoverResult::Failed) throw Stop{Outcome::Failed, msg, L"Do not delete the .redot-update folder. Re-run the updater after closing ReDot, or restore files from the backup folder inside it manually."};
        res.notice = msg;

        InstallInfo info; std::wstring why;
        if (identifyInstall(install, info, why) != IdentStatus::Ok) throw Stop{Outcome::Failed, why, L"Choose the folder that contains a released ReDot.exe."};
        res.oldVersion = versionString(info.version); res.legacy = info.legacyInferred;

        rep.status(L"Checking GitHub for the latest ReDot release...");
        rep.progress(-1);
        std::string body; std::wstring nerr;
        if (!net.getText(o.releasesUrl, body, nerr)) throw Stop{Outcome::Failed, L"Could not reach GitHub: " + nerr, L"Check your internet connection and run the updater again. Your ReDot was not changed."};
        FeedResult feed = selectLatestRelease(body);
        if (feed.status == FeedStatus::BadFeed) throw Stop{Outcome::Failed, L"GitHub returned an unexpected response: " + W(feed.detail), L"Try again later. Your ReDot was not changed."};
        if (feed.status == FeedStatus::NoStableRelease) throw Stop{Outcome::Failed, L"No stable ReDot release was found on GitHub.", L"Your ReDot was not changed."};
        if (feed.status == FeedStatus::MissingAssets) throw Stop{Outcome::Failed, L"The newest release is not ready to install: " + W(feed.detail), L"Try again later or download the ZIP manually from the releases page. Your ReDot was not changed."};
        res.newVersion = versionString(feed.release.version); res.notes = feed.release.notes;
        if (compareVersions(feed.release.version, info.version) <= 0) {
            res.outcome = Outcome::UpToDate; res.message = L"ReDot " + W(res.oldVersion) + L" is up to date."; return res;
        }
        if (o.checkOnly) { res.outcome = Outcome::UpdateAvailable; res.message = L"ReDot " + W(res.newVersion) + L" is available (installed " + W(res.oldVersion) + L")."; return res; }

        // Never touch a running ReDot (it may be recording). The user closes it normally; we wait.
        const fs::path exe = install / L"ReDot.exe";
        if (isReDotRunning(exe) && !rep.waitForReDotClosed(exe)) throw Stop{Outcome::Cancelled, L"Update cancelled. ReDot was not changed.", L""};

        // Controlled working area inside the install folder (same volume => atomic renames). Remove only our stale leftovers.
        fs::create_directories(work, ec);
        if (ec || !fs::is_directory(work)) throw Stop{Outcome::Failed, L"The ReDot folder is not writable.", L"Move ReDot to a normal folder (for example Documents\\ReDot) or run the updater as an administrator."};
        for (fs::directory_iterator it(work, ec), end; !ec && it != end; it.increment(ec)) {
            std::wstring n = it->path().filename().wstring();
            if (n == L"download" || n.rfind(L"staging-", 0) == 0) { std::error_code e2; fs::remove_all(it->path(), e2); }
        }
        const fs::path dl = work / L"download";
        fs::create_directories(dl, ec);
        ULARGE_INTEGER freeBytes{};
        const uint64_t need = feed.release.zip.size * 3 + (200ull << 20);
        if (GetDiskFreeSpaceExW(install.c_str(), &freeBytes, nullptr, nullptr) && freeBytes.QuadPart < need)
            throw Stop{Outcome::Failed, L"Not enough free disk space (about " + fmtMB(need) + L" needed).", L"Free some space and run the updater again."};

        rep.status(L"Downloading ReDot " + W(res.newVersion) + L"...");
        const fs::path sigFile = dl / L"package.sig", zipPart = dl / L"package.zip.part", zipFile = dl / L"package.zip";
        if (!net.download(W(feed.release.sig.url), sigFile, 0, nullptr, nerr)) throw Stop{Outcome::Failed, L"Could not download the package signature: " + nerr, L"Your ReDot was not changed."};
        if (cancelled()) throw Stop{Outcome::Cancelled, L"Update cancelled. ReDot was not changed.", L""};
        const uint64_t expectSize = feed.release.zip.size;
        if (!net.download(W(feed.release.zip.url), zipPart, expectSize, [&](uint64_t d, uint64_t t) {
                if (t) rep.progress(0.55 * static_cast<double>(d) / static_cast<double>(t));
                return !cancelled(); }, nerr)) {
            fs::remove(zipPart, ec);
            if (cancelled()) throw Stop{Outcome::Cancelled, L"Update cancelled. ReDot was not changed.", L""};
            throw Stop{Outcome::Failed, L"The download failed: " + nerr, L"Check your internet connection and run the updater again. Your ReDot was not changed."};
        }
        if (expectSize && fs::file_size(zipPart, ec) != expectSize) { fs::remove(zipPart, ec); throw Stop{Outcome::Failed, L"The download is incomplete or the wrong size.", L"Run the updater again. Your ReDot was not changed."}; }
        fs::rename(zipPart, zipFile, ec);

        rep.status(L"Verifying the download's signature...");
        std::vector<uint8_t> digest, sig; std::wstring herr;
        if (!sha256File(zipFile, digest, [&](uint64_t d, uint64_t t) { rep.progress(0.55 + 0.07 * static_cast<double>(d) / static_cast<double>(std::max<uint64_t>(t, 1))); return !cancelled(); }, herr)) throw Stop{cancelled() ? Outcome::Cancelled : Outcome::Failed, cancelled() ? L"Update cancelled. ReDot was not changed." : herr, L"Your ReDot was not changed."};
        { std::string s = readSmallFile(sigFile, 4096); sig.assign(s.begin(), s.end()); }
        if (!verifyP256(o.publicKey, o.publicKeySize, digest, sig)) {
            fs::remove(zipFile, ec);
            throw Stop{Outcome::Failed, L"The downloaded package failed its authenticity check and was discarded.", L"Do not install it. Your ReDot was not changed. Download ReDot only from https://github.com/Eyal2012/redot."};
        }

        rep.status(L"Unpacking...");
        ZipPackage pkg;
        if (!openZip(zipFile, pkg, herr)) throw Stop{Outcome::Failed, herr, L"Your ReDot was not changed."};
        const fs::path staging = work / (L"staging-" + std::to_wstring(GetTickCount64()));
        if (!extractZip(pkg, staging, [&](uint64_t d, uint64_t t) { rep.progress(0.62 + 0.28 * static_cast<double>(d) / static_cast<double>(std::max<uint64_t>(t, 1))); return !cancelled(); }, herr)) {
            fs::remove_all(staging, ec);
            throw Stop{cancelled() ? Outcome::Cancelled : Outcome::Failed, cancelled() ? L"Update cancelled. ReDot was not changed." : herr, L"Your ReDot was not changed."};
        }
        // The signed package must describe exactly the release we think we are installing.
        { Json m; std::string perr; Version pv;
          if (!parseJson(readSmallFile(staging / L"redot-version.json"), m, perr) || m.str("product") != "ReDot" || !parseVersion(m.str("version"), pv) || compareVersions(pv, feed.release.version) != 0 || m.str("platform") != "win-x64") {
              fs::remove_all(staging, ec); throw Stop{Outcome::Failed, L"The package manifest does not match the release.", L"Your ReDot was not changed."}; }
          if (static_cast<int>(m.num("updaterProtocol", 1)) > kUpdaterProtocol) {
              fs::remove_all(staging, ec); throw Stop{Outcome::Failed, L"This release needs a newer ReDotUpdater.", L"Download the latest ReDotUpdater.exe from https://github.com/Eyal2012/redot/releases and run it. Your ReDot was not changed."}; } }
        if (isReDotRunning(exe) && !rep.waitForReDotClosed(exe)) { fs::remove_all(staging, ec); throw Stop{Outcome::Cancelled, L"Update cancelled. ReDot was not changed.", L""}; }

        rep.status(L"Installing ReDot " + W(res.newVersion) + L"...");
        rep.progress(0.92);
        std::vector<std::string> rels;
        for (auto& e : pkg.entries) if (!e.isDir) rels.push_back(e.name);
        InstallOptions io; io.installDir = install; io.stagingDir = staging; io.oldVersion = res.oldVersion; io.failHook = o.failHook; io.crashAtIndex = o.crashAtIndex;
        std::wstring ierr;
        if (!installStaged(io, rels, ierr)) throw Stop{Outcome::Failed, ierr, L"Close ReDot and anything using its folder, then run the updater again. If problems continue, the previous files are kept in the .redot-update folder inside the ReDot folder."};
        fs::remove_all(staging, ec); fs::remove_all(dl, ec);
        rep.progress(1.0);
        res.outcome = Outcome::Updated; res.message = L"ReDot was updated from " + W(res.oldVersion) + L" to " + W(res.newVersion) + L".";
        return res;
    } catch (const Stop& s) {
        res.outcome = s.outcome; res.message = s.message; res.recovery = s.recovery;
        return res;
    } catch (const std::exception& e) {
        res.outcome = Outcome::Failed; res.message = L"Unexpected error: " + W(e.what()); res.recovery = L"Run the updater again. If a previous update was interrupted it will be rolled back first.";
        return res;
    }
}

}  // namespace rdu
