// Focused updater tests. Everything runs inside the given work directory (under build/); no personal ReDot,
// no network, no GitHub. Usage: ReDotUpdaterTests <workdir> [--legacy-exe <v1.0.0 ReDot.exe>] [--real-zip <zip>]
#include "core.h"
#include "public_key.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>

using namespace rdu;
static int g_pass = 0, g_fail = 0;
#define CHECK(cond, name) do { if (cond) { ++g_pass; printf("PASS %s\n", name); } else { ++g_fail; printf("FAIL %s   [%s:%d]\n", name, __FILE__, __LINE__); } } while (0)

// ---- helpers --------------------------------------------------------------------------------------------
static void writeFile(const fs::path& p, const std::string& data) {
    std::error_code ec; fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary); f.write(data.data(), static_cast<std::streamsize>(data.size()));
}
static std::string readFile(const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), {}); }
static std::string hexOf(const fs::path& p) {
    std::vector<uint8_t> d; std::wstring e; if (!sha256File(p, d, nullptr, e)) return "ERR";
    std::string s; char t[3]; for (auto b : d) { sprintf_s(t, "%02x", b); s += t; } return s;
}
static std::string textBlob(size_t n, unsigned seed) {   // compressible pseudo-text (exercises dynamic Huffman)
    static const char* words[] = {"replay", "buffer", "capture", "encoder", "audio", "scene", "ReDot", "frame", "clip", "stream", "hotkey", "update"};
    std::string s; unsigned x = seed;
    while (s.size() < n) { x = x * 1664525u + 1013904223u; s += words[(x >> 16) % 12]; s += (x & 0x100) ? ' ' : '\n'; }
    s.resize(n); return s;
}
static std::string randomBlob(size_t n, unsigned seed) { std::string s(n, 0); unsigned x = seed; for (auto& c : s) { x = x * 1103515245u + 12345u; c = static_cast<char>(x >> 16); } return s; }
using Snapshot = std::map<std::string, std::string>;
static Snapshot snapshot(const fs::path& dir) {
    Snapshot s; std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        auto rel = fs::relative(it->path(), dir).generic_string();
        if (rel.rfind(".redot-update", 0) == 0) continue;
        s[rel] = hexOf(it->path());
    }
    return s;
}

// Minimal ZIP writer (stored entries) with knobs to build hostile archives.
struct ZEntry { std::string name, data; uint16_t flags = 0; uint32_t extAttr = 0; uint16_t madeBy = 20; int crcXor = 0; };
static uint32_t crc32of(const std::string& d) { uint32_t c = 0xFFFFFFFFu; for (unsigned char b : d) { c ^= b; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; } return ~c; }
static void put16(std::string& s, uint16_t v) { s += static_cast<char>(v); s += static_cast<char>(v >> 8); }
static void put32(std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s += static_cast<char>(v >> (8 * i)); }
static void makeZip(const fs::path& path, const std::vector<ZEntry>& es) {
    std::string out, cd;
    for (auto& e : es) {
        uint32_t off = static_cast<uint32_t>(out.size()), crc = crc32of(e.data) ^ static_cast<uint32_t>(e.crcXor);
        put32(out, 0x04034b50); put16(out, 20); put16(out, e.flags); put16(out, 0); put16(out, 0); put16(out, 0x21); put32(out, crc);
        put32(out, static_cast<uint32_t>(e.data.size())); put32(out, static_cast<uint32_t>(e.data.size())); put16(out, static_cast<uint16_t>(e.name.size())); put16(out, 0);
        out += e.name; out += e.data;
        put32(cd, 0x02014b50); put16(cd, e.madeBy); put16(cd, 20); put16(cd, e.flags); put16(cd, 0); put16(cd, 0); put16(cd, 0x21); put32(cd, crc);
        put32(cd, static_cast<uint32_t>(e.data.size())); put32(cd, static_cast<uint32_t>(e.data.size())); put16(cd, static_cast<uint16_t>(e.name.size()));
        put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0); put32(cd, e.extAttr); put32(cd, off); cd += e.name;
    }
    uint32_t cdOff = static_cast<uint32_t>(out.size());
    out += cd; put32(out, 0x06054b50); put16(out, 0); put16(out, 0); put16(out, static_cast<uint16_t>(es.size())); put16(out, static_cast<uint16_t>(es.size()));
    put32(out, static_cast<uint32_t>(cd.size())); put32(out, cdOff); put16(out, 0);
    writeFile(path, out);
}
static std::string manifest(const char* ver, int proto = 1) {
    return std::string("{\"product\":\"ReDot\",\"version\":\"") + ver + "\",\"platform\":\"win-x64\",\"updaterProtocol\":" + std::to_string(proto) + "}";
}
static int cmpV(const char* a, const char* b) { Version x, y; if (!parseVersion(a, x) || !parseVersion(b, y)) return 99; return compareVersions(x, y); }

struct FileNet : INet {
    std::map<std::wstring, fs::path> files; int downloads = 0; std::wstring failUrl;
    bool getText(const std::wstring& url, std::string& body, std::wstring& err) override {
        auto it = files.find(url); if (it == files.end()) { err = L"HTTP 404"; return false; } body = readFile(it->second); return true; }
    bool download(const std::wstring& url, const fs::path& dest, uint64_t, const ProgressFn& progress, std::wstring& err) override {
        ++downloads;
        auto it = files.find(url); if (it == files.end() || url == failUrl) { err = L"HTTP 404 / simulated network failure"; return false; }
        std::error_code ec; fs::copy_file(it->second, dest, fs::copy_options::overwrite_existing, ec);
        if (progress) progress(1, 1);
        return !ec;
    }
};
struct TestRep : Reporter {
    bool closeResult = true; int waits = 0;
    void status(const std::wstring&) override {}
    void progress(double) override {}
    bool waitForReDotClosed(const fs::path&) override { ++waits; return closeResult; }
};

static std::string releasesJson(bool withAssets, const char* urlBase = "https://github.com/Eyal2012/redot/releases/download/") {
    auto assets = [&](const char* v) {
        if (!withAssets) return std::string("[]");
        std::string z = std::string("ReDot-v") + v + "-Windows-x64.zip";
        return "[{\"name\":\"" + z + "\",\"size\":1000,\"browser_download_url\":\"" + urlBase + "v" + v + "/" + z + "\"},"
               "{\"name\":\"" + z + ".sig\",\"size\":64,\"browser_download_url\":\"" + urlBase + "v" + v + "/" + z + ".sig\"}]"; };
    return "[{\"tag_name\":\"v1.1.0\",\"draft\":false,\"prerelease\":true,\"assets\":" + assets("1.1.0") + "},"
           "{\"tag_name\":\"v2.0.0\",\"draft\":true,\"prerelease\":false,\"assets\":" + assets("2.0.0") + "},"
           "{\"tag_name\":\"v1.0.2-rc.1\",\"draft\":false,\"prerelease\":false,\"assets\":" + assets("1.0.2-rc.1") + "},"
           "{\"tag_name\":\"latest\",\"draft\":false,\"prerelease\":false,\"assets\":[]},"
           "{\"tag_name\":\"v1.0.1\",\"draft\":false,\"prerelease\":false,\"body\":\"Fixes the Save Clip hotkey. caf\\u00e9 \\ud83d\\ude00\",\"assets\":" + assets("1.0.1") + "},"
           "{\"tag_name\":\"v1.0.0\",\"draft\":false,\"prerelease\":false,\"assets\":" + assets("1.0.0") + "}]";
}

// ---- tests ----------------------------------------------------------------------------------------------
static void testVersions() {
    CHECK(cmpV("1.0.0", "1.0.1") < 0, "version 1.0.0 < 1.0.1");
    CHECK(cmpV("v1.0.1", "1.0.1") == 0, "leading v ignored");
    CHECK(cmpV("1.0.10", "1.0.9") > 0, "1.0.10 > 1.0.9 (numeric, not text)");
    CHECK(cmpV("1.1.0", "1.0.99") > 0 && cmpV("2.0.0", "1.99.99") > 0, "minor/major ordering");
    CHECK(cmpV("1.0.0-beta.1", "1.0.0") < 0 && cmpV("1.0.0-beta.2", "1.0.0-beta.10") < 0 && cmpV("1.0.0-alpha", "1.0.0-beta") < 0, "pre-release ordering");
    CHECK(cmpV("1.0.1+build7", "1.0.1") == 0, "build metadata ignored");
    Version v; CHECK(!parseVersion("1.0", v) && !parseVersion("abc", v) && !parseVersion("", v) && !parseVersion("1.0.x", v) && !parseVersion("v1..0", v), "malformed versions rejected");
    Version legacy{0, 3, 0, {}}, pub{1, 0, 0, {}}; CHECK(compareVersions(legacy, pub) < 0, "embedded 0.3.0 would wrongly look older (handled explicitly below)");
}

static void testFeed() {
    auto f = selectLatestRelease(releasesJson(true));
    CHECK(f.status == FeedStatus::Ok && f.release.tag == "v1.0.1", "feed: newest stable release chosen (drafts, prereleases, rc tags, junk tags ignored)");
    CHECK(f.release.zip.name == "ReDot-v1.0.1-Windows-x64.zip" && f.release.sig.name == f.release.zip.name + ".sig", "feed: official ZIP + signature assets found");
    CHECK(f.release.notes.find("caf\xC3\xA9 \xF0\x9F\x98\x80") != std::string::npos, "feed: release notes decode \\u escapes and surrogate pairs");
    CHECK(selectLatestRelease(releasesJson(false)).status == FeedStatus::MissingAssets, "feed: stable release without assets reported, not skipped");
    CHECK(selectLatestRelease(releasesJson(true, "https://github.com/evil/redot/releases/download/")).status == FeedStatus::MissingAssets, "feed: assets outside the official repo ignored");
    CHECK(selectLatestRelease("[{\"tag_name\":\"v3.0.0\",\"prerelease\":true,\"draft\":false,\"assets\":[]}]").status == FeedStatus::NoStableRelease, "feed: only prereleases => no stable release");
    CHECK(selectLatestRelease("not json").status == FeedStatus::BadFeed && selectLatestRelease("{\"message\":\"rate limit\"}").status == FeedStatus::BadFeed, "feed: garbage / error object rejected");
}

static void testDetection(const fs::path& work, const fs::path& legacyExe) {
    fs::path root = work / "detect";
    fs::path legacyDir = root / "Downloads" / "ReDot-v1.0.0-Windows-x64" / "ReDot-v1.0.0", dev = root / "Downloads" / "redot-dev-build", good = root / "Documents" / "ReDot", empty = root / "Desktop" / "ReDot-empty", badManifest = root / "Documents" / "ReDot-badmanifest";
    std::error_code ec;
    if (fs::exists(legacyExe, ec)) {
        fs::create_directories(legacyDir); fs::copy_file(legacyExe, legacyDir / "ReDot.exe"); writeFile(legacyDir / "THIRD-PARTY-NOTICES.txt", "x");
        fs::create_directories(dev); fs::copy_file(legacyExe, dev / "ReDot.exe"); writeFile(dev / "README.md", "dev build");
    }
    writeFile(good / "ReDot.exe", "MZ-dummy"); writeFile(good / "redot-version.json", manifest("1.0.1"));
    fs::create_directories(empty);
    writeFile(badManifest / "ReDot.exe", "MZ"); writeFile(badManifest / "redot-version.json", "{\"product\":\"Other\",\"version\":\"1.0.0\"}");
    InstallInfo info; std::wstring why;
    CHECK(identifyInstall(good, info, why) == IdentStatus::Ok && versionString(info.version) == "1.0.1" && !info.legacyInferred, "detect: manifest install reports its manifest version");
    CHECK(identifyInstall(empty, info, why) == IdentStatus::NotReDot, "detect: folder without ReDot.exe is refused (manual selection validation)");
    CHECK(identifyInstall(badManifest, info, why) == IdentStatus::Unrecognized, "detect: foreign/invalid manifest refused");
    auto found = findInstallations({root / "Downloads", root / "Documents", root / "Desktop"});
    if (fs::exists(legacyExe, ec)) {
        CHECK(identifyInstall(legacyDir, info, why) == IdentStatus::Ok && info.legacyInferred && versionString(info.version) == "1.0.0", "detect: public v1.0.0 (exe reports 0.3.0) is treated explicitly as 1.0.0");
        CHECK(identifyInstall(dev, info, why) == IdentStatus::Unrecognized, "detect: development build (0.3.0, no manifest, no THIRD-PARTY-NOTICES) refused");
        CHECK(found.size() == 2, "detect: auto-search finds legacy + manifest installs only (nested ZIP folder, dev build and empty folder skipped)");
    } else printf("SKIP legacy-exe detection checks (exe not provided)\n");
}

static void testZipSafety(const fs::path& work) {
    fs::path d = work / "zip"; fs::create_directories(d);
    ZEntry exe{"ReDot.exe", "EXE"}, man{"redot-version.json", manifest("1.0.1")};
    int n = 0;
    auto rejected = [&](const char* name, std::vector<ZEntry> es) {
        fs::path z = d / ("bad" + std::to_string(++n) + ".zip"); makeZip(z, es);
        ZipPackage p; std::wstring err; CHECK(!openZip(z, p, err), name);
    };
    rejected("zip: '../' traversal rejected", {exe, man, {"../evil.txt", "x"}});
    rejected("zip: backslash traversal rejected", {exe, man, {"..\\evil.txt", "x"}});
    rejected("zip: deep traversal rejected", {exe, man, {"a/b/../../../c.txt", "x"}});
    rejected("zip: drive-letter path rejected", {exe, man, {"C:\\evil.txt", "x"}});
    rejected("zip: absolute path rejected", {exe, man, {"/evil.txt", "x"}});
    rejected("zip: NTFS stream name rejected", {exe, man, {"ReDot.exe:evil", "x"}});
    rejected("zip: reserved device name rejected", {exe, man, {"CON.txt", "x"}});
    rejected("zip: reserved .redot-update path rejected", {exe, man, {".redot-update/x", "x"}});
    rejected("zip: user-data file (settings-v1.txt) rejected", {exe, man, {"settings-v1.txt", "x"}});
    rejected("zip: PandaClip data folder rejected", {exe, man, {"PandaClip/scenes-v1.json", "x"}});
    rejected("zip: duplicate names rejected", {exe, man, {"readme.txt", "a"}, {"README.TXT", "b"}});
    { ZEntry enc = exe; enc.flags = 1; rejected("zip: encrypted entry rejected", {enc, man}); }
    { ZEntry ln{"link", "target"}; ln.madeBy = 0x031E; ln.extAttr = 0xA1FF0000u; rejected("zip: symlink entry rejected", {exe, man, ln}); }
    rejected("zip: missing ReDot.exe rejected", {man, {"other.txt", "x"}});
    rejected("zip: missing redot-version.json rejected", {exe, {"other.txt", "x"}});
    { fs::path z = d / "trunc.zip"; makeZip(z, {exe, man}); std::string s = readFile(z); s.resize(s.size() - 30); writeFile(z, s);
      ZipPackage p; std::wstring err; CHECK(!openZip(z, p, err), "zip: truncated archive rejected"); }
    { fs::path z = d / "notzip.zip"; writeFile(z, randomBlob(5000, 3)); ZipPackage p; std::wstring err; CHECK(!openZip(z, p, err), "zip: random bytes rejected"); }
    { fs::path z = d / "crc.zip"; ZEntry bad = {"data.bin", randomBlob(1000, 9)}; bad.crcXor = 1; makeZip(z, {exe, man, bad});
      ZipPackage p; std::wstring err; bool opened = openZip(z, p, err);
      CHECK(opened && !extractZip(p, d / "crc-staging", nullptr, err), "zip: CRC mismatch detected during extraction"); }
    { fs::path z = d / "good.zip"; makeZip(z, {{"ReDot-v9/", ""}, {"ReDot-v9/ReDot.exe", "EXE"}, {"ReDot-v9/redot-version.json", manifest("1.0.1")}, {"ReDot-v9/sub/a.txt", "hello"}});
      ZipPackage p; std::wstring err; bool ok = openZip(z, p, err) && extractZip(p, d / "good-staging", nullptr, err);
      if (!ok) printf("   zip error: %ls\n", err.c_str());
      CHECK(ok && p.strippedRoot == "ReDot-v9/" && readFile(d / "good-staging" / "sub" / "a.txt") == "hello" && !fs::exists(d / "evil.txt"), "zip: valid package extracts, single container folder stripped, nothing written outside staging"); }
}

// A mock portable install + signed package built with real deflate (via Windows tar.exe).
struct Mock {
    fs::path root, install, pkgZip, sigFile, feedFile; Snapshot original; std::vector<uint8_t> pub, priv;
    FileNet net;
};
static void makeInstall(const fs::path& install) {
    std::error_code ec; fs::remove_all(install, ec);
    writeFile(install / "ReDot.exe", "OLD-REDOT-EXE-" + randomBlob(2000, 1));
    writeFile(install / "Qt6Core.dll", textBlob(300000, 11));
    writeFile(install / "redot-version.json", manifest("1.0.0"));
    writeFile(install / "THIRD-PARTY-NOTICES.txt", "old notices");
    writeFile(install / "docs" / "readme.md", "old readme");
    writeFile(install / "my-clips" / "clip1.mp4", randomBlob(60000, 21));      // user data inside the folder
    writeFile(install / "notes.txt", "my private notes");
    writeFile(install / "settings-v1.txt", "PandaClip settings 1\r\n\"binding0\" \"120\"\r\n");
    writeFile(install / "PandaClip" / "scenes-v1.json", "{\"scenes\":[1,2,3]}");
    writeFile(install / "extra-plugin.dll", "not part of any package");
}
static bool buildMock(const fs::path& root, Mock& m, const char* version = "1.0.1", int proto = 1) {
    m.net = FileNet(); m.root = root; m.install = root / "ReDot";
    makeInstall(m.install); m.original = snapshot(m.install);
    fs::path pkg = root / "pkgsrc" / ("ReDot-v" + std::string(version));
    std::error_code ec; fs::remove_all(root / "pkgsrc", ec);
    writeFile(pkg / "ReDot.exe", "NEW-REDOT-EXE-" + randomBlob(3000, 2));
    writeFile(pkg / "Qt6Core.dll", textBlob(400000, 12));
    writeFile(pkg / "redot-version.json", manifest(version, proto));
    writeFile(pkg / "THIRD-PARTY-NOTICES.txt", "new notices");
    writeFile(pkg / "docs" / "readme.md", "new readme");
    writeFile(pkg / "newdir" / "deep" / "new.txt", "brand new file");
    m.pkgZip = root / ("ReDot-v" + std::string(version) + "-Windows-x64.zip");
    fs::remove(m.pkgZip, ec);
    wchar_t sysdir[MAX_PATH]; GetSystemDirectoryW(sysdir, MAX_PATH);   // Windows bsdtar, not another tar earlier in PATH
    std::wstring cmdline = L"\"\"" + (fs::path(sysdir) / L"tar.exe").wstring() + L"\" -a -cf \"" + m.pkgZip.wstring() + L"\" -C \"" + (root / "pkgsrc").wstring() + L"\" ReDot-v" + W(version) + L"\"";
    if (_wsystem(cmdline.c_str()) != 0 || !fs::exists(m.pkgZip)) return false;
    generateP256(m.priv, m.pub);
    std::vector<uint8_t> digest, sig; std::wstring err; sha256File(m.pkgZip, digest, nullptr, err); signDigest(m.priv, digest, sig);
    m.sigFile = root / "package.sig"; writeFile(m.sigFile, std::string(sig.begin(), sig.end()));
    m.feedFile = root / "releases.json"; writeFile(m.feedFile, releasesJson(true).substr(0));
    std::string base = "https://github.com/Eyal2012/redot/releases/download/v" + std::string(version) + "/ReDot-v" + version + "-Windows-x64.zip";
    // Feed advertising `version` as the only stable release.
    std::string z = std::string("ReDot-v") + version + "-Windows-x64.zip";
    writeFile(m.feedFile, "[{\"tag_name\":\"v" + std::string(version) + "\",\"draft\":false,\"prerelease\":false,\"body\":\"notes\",\"assets\":[{\"name\":\"" + z + "\",\"size\":" + std::to_string(fs::file_size(m.pkgZip)) +
              ",\"browser_download_url\":\"" + base + "\"},{\"name\":\"" + z + ".sig\",\"size\":64,\"browser_download_url\":\"" + base + ".sig\"}]}]");
    m.net.files = {{kReleasesApi, m.feedFile}, {W(base), m.pkgZip}, {W(base + ".sig"), m.sigFile}};
    return true;
}
static Options opts(const Mock& m) { Options o; o.installDir = m.install; o.publicKey = m.pub.data(); o.publicKeySize = m.pub.size(); return o; }
static bool userFilesIntact(const Mock& m) {
    for (auto rel : {"my-clips/clip1.mp4", "notes.txt", "settings-v1.txt", "PandaClip/scenes-v1.json", "extra-plugin.dll"})
        if (hexOf(m.install / rel) != m.original.at(rel)) return false;
    return true;
}

static void testUpdateAndRollback(const fs::path& work) {
    Mock m; TestRep rep;
    if (!buildMock(work / "m1", m)) { CHECK(false, "mock package could not be built with tar.exe"); return; }
    // --- successful update
    RunResult r = runUpdate(opts(m), m.net, rep);
    if (r.outcome != Outcome::Updated) printf("   update message: %ls | %ls\n", r.message.c_str(), r.recovery.c_str());
    CHECK(r.outcome == Outcome::Updated && r.oldVersion == "1.0.0" && r.newVersion == "1.0.1", "update: v1.0.0 -> v1.0.1 succeeds");
    CHECK(readFile(m.install / "ReDot.exe").rfind("NEW-REDOT-EXE-", 0) == 0 && readFile(m.install / "Qt6Core.dll") == textBlob(400000, 12) && readFile(m.install / "docs" / "readme.md") == "new readme", "update: application files replaced (real deflate decoded bit-exactly)");
    CHECK(readFile(m.install / "newdir" / "deep" / "new.txt") == "brand new file", "update: new files/folders added");
    CHECK(userFilesIntact(m), "update: clips, notes, settings, scenes and unrelated files preserved byte-for-byte");
    InstallInfo info; std::wstring why;
    CHECK(identifyInstall(m.install, info, why) == IdentStatus::Ok && versionString(info.version) == "1.0.1", "update: manifest now reports 1.0.1");
    bool leftovers = false, backup = false; for (auto& e : fs::directory_iterator(m.install / ".redot-update")) { auto n = e.path().filename().string(); if (n.rfind("staging-", 0) == 0 || n == "download" || n == "journal.txt") leftovers = true; if (n.rfind("backup-1.0.0", 0) == 0) backup = true; }
    CHECK(!leftovers && backup, "update: staging/download/journal cleaned, previous version kept in backup");
    CHECK(runUpdate(opts(m), m.net, rep).outcome == Outcome::UpToDate, "update: running again reports up to date");

    // --- failure after 3 files are swapped => full rollback
    buildMock(work / "m2", m);
    Options o = opts(m); o.failHook = [](size_t i, const std::string&) { return i == 3; };
    r = runUpdate(o, m.net, rep);
    CHECK(r.outcome == Outcome::Failed && r.message.find(L"restored") != std::wstring::npos, "rollback: simulated mid-install failure reports that the old version was restored");
    CHECK(snapshot(m.install) == m.original, "rollback: every original file restored byte-for-byte, new files/folders removed");

    // --- crash (no rollback in-process) then recovery from the journal
    buildMock(work / "m3", m);
    o = opts(m); o.crashAtIndex = 2;
    r = runUpdate(o, m.net, rep);
    CHECK(r.outcome == Outcome::Failed && snapshot(m.install) != m.original, "recovery: simulated crash leaves a half-installed folder");
    std::wstring msg; CHECK(recoverInterrupted(m.install, msg) == RecoverResult::RolledBack && snapshot(m.install) == m.original, "recovery: interrupted update rolled back from the journal");
    buildMock(work / "m3b", m); o = opts(m); o.crashAtIndex = 1; runUpdate(o, m.net, rep);
    r = runUpdate(opts(m), m.net, rep);
    CHECK(r.outcome == Outcome::Updated && !r.notice.empty() && userFilesIntact(m), "recovery: next run rolls back first, tells the user, then completes the update");

    // --- authenticity
    buildMock(work / "m4", m);
    { std::string s = readFile(m.sigFile); s[10] ^= 1; writeFile(m.sigFile, s); }
    r = runUpdate(opts(m), m.net, rep);
    CHECK(r.outcome == Outcome::Failed && r.message.find(L"authenticity") != std::wstring::npos && snapshot(m.install) == m.original, "auth: tampered signature rejected, install untouched");
    buildMock(work / "m5", m);
    { std::string s = readFile(m.pkgZip); s[s.size() / 2] ^= 1; writeFile(m.pkgZip, s); }
    r = runUpdate(opts(m), m.net, rep);
    CHECK(r.outcome == Outcome::Failed && snapshot(m.install) == m.original, "auth: tampered ZIP rejected (signature no longer matches), install untouched");
    buildMock(work / "m6", m); { std::vector<uint8_t> p2, k2; generateP256(k2, p2); Options w = opts(m); w.publicKey = p2.data(); w.publicKeySize = p2.size(); r = runUpdate(w, m.net, rep); }
    CHECK(r.outcome == Outcome::Failed && snapshot(m.install) == m.original, "auth: package signed by a different key rejected");

    // --- network / feed failures and protocol
    buildMock(work / "m7", m); m.net.failUrl = W("https://github.com/Eyal2012/redot/releases/download/v1.0.1/ReDot-v1.0.1-Windows-x64.zip");
    r = runUpdate(opts(m), m.net, rep);
    CHECK(r.outcome == Outcome::Failed && snapshot(m.install) == m.original, "network: failed download reported, install untouched");
    buildMock(work / "m8", m); m.net.files.erase(kReleasesApi);
    r = runUpdate(opts(m), m.net, rep);
    CHECK(r.outcome == Outcome::Failed && r.message.find(L"GitHub") != std::wstring::npos, "network: unreachable GitHub reported clearly");
    buildMock(work / "m9", m, "1.0.1", 99);
    r = runUpdate(opts(m), m.net, rep);
    CHECK(r.outcome == Outcome::Failed && r.message.find(L"newer ReDotUpdater") != std::wstring::npos && snapshot(m.install) == m.original, "compat: package needing a newer updater refused, install untouched");
}

static void testRunning(const fs::path& work) {
    Mock m; TestRep rep;
    if (!buildMock(work / "mrun", m)) { CHECK(false, "mock build"); return; }
    // A harmless owned process named ReDot.exe launched from the mock install.
    fs::path exe = m.install / "ReDot.exe", saved = work / "mrun" / "ReDot.exe.orig";
    fs::copy_file(exe, saved, fs::copy_options::overwrite_existing);
    wchar_t sys[MAX_PATH]; GetSystemDirectoryW(sys, MAX_PATH);
    fs::copy_file(fs::path(sys) / "ping.exe", exe, fs::copy_options::overwrite_existing);
    STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{}; std::wstring cl = L"\"" + exe.wstring() + L"\" -n 40 127.0.0.1";
    bool started = CreateProcessW(exe.c_str(), cl.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != 0;
    CHECK(started && isReDotRunning(exe), "running: process named ReDot.exe in the install folder is detected");
    // Snapshot with the stand-in exe so "unchanged" is meaningful.
    Snapshot before = snapshot(m.install);
    rep.closeResult = false;
    RunResult r = runUpdate(opts(m), m.net, rep);
    CHECK(r.outcome == Outcome::Cancelled && rep.waits == 1 && snapshot(m.install) == before, "running: update waits for ReDot to close; cancelling leaves everything untouched");
    if (started) { TerminateProcess(pi.hProcess, 0); WaitForSingleObject(pi.hProcess, 3000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    CHECK(!isReDotRunning(exe), "running: no longer reported after the process exits");
}

static void testRealZip(const fs::path& work, const fs::path& zip) {
    auto t0 = std::chrono::steady_clock::now();
    ZipPackage p; std::wstring err;
    bool ok = openZip(zip, p, err, false) && extractZip(p, work / "realzip-staging", nullptr, err);
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("   real public v1.0.0 ZIP: %zu entries, %.0f MB unpacked in %.1f s\n", p.entries.size(), static_cast<double>(p.totalSize) / 1048576.0, s);
    CHECK(ok, "real v1.0.0 ZIP (152 MB, real deflate) extracts with every CRC matching");
    if (!ok) printf("   error: %ls\n", err.c_str());
    std::error_code ec; fs::remove_all(work / "realzip-staging", ec);
}

// Dress rehearsal with the REAL signed release files and the REAL embedded public key against a copy of the public v1.0.0 package.
//   ReDotUpdaterTests --rehearsal <install copy> <zip> <sig> <expected new ReDot.exe>
static int rehearsal(const fs::path& install, const fs::path& zip, const fs::path& sig, const fs::path& expectedExe) {
    const std::string v = "1.0.1", name = "ReDot-v1.0.1-Windows-x64.zip", base = "https://github.com/Eyal2012/redot/releases/download/v1.0.1/" + name;
    writeFile(install / "my-clips" / "clip1.mp4", randomBlob(60000, 5)); writeFile(install / "notes.txt", "private");
    Snapshot before = snapshot(install);
    fs::path feed = install.parent_path() / "rehearsal-feed.json";
    writeFile(feed, "[{\"tag_name\":\"v1.0.1\",\"draft\":false,\"prerelease\":false,\"body\":\"x\",\"assets\":[{\"name\":\"" + name + "\",\"size\":" + std::to_string(fs::file_size(zip)) +
              ",\"browser_download_url\":\"" + base + "\"},{\"name\":\"" + name + ".sig\",\"size\":64,\"browser_download_url\":\"" + base + ".sig\"}]}]");
    FileNet net; net.files = {{kReleasesApi, feed}, {W(base), zip}, {W(base + ".sig"), sig}};
    Options o; o.installDir = install; o.publicKey = kReleasePublicKey; o.publicKeySize = kReleasePublicKeySize;
    TestRep rep; InstallInfo info; std::wstring why;
    CHECK(identifyInstall(install, info, why) == IdentStatus::Ok && info.legacyInferred && versionString(info.version) == "1.0.0", "rehearsal: copy of the public v1.0.0 package is recognised as 1.0.0 (embedded exe says 0.3.0)");
    RunResult r = runUpdate(o, net, rep);
    if (r.outcome != Outcome::Updated) printf("   %ls | %ls\n", r.message.c_str(), r.recovery.c_str());
    CHECK(r.outcome == Outcome::Updated, "rehearsal: real signed v1.0.1 package installs over v1.0.0 with the real embedded key");
    CHECK(hexOf(install / "ReDot.exe") == hexOf(expectedExe) && identifyInstall(install, info, why) == IdentStatus::Ok && versionString(info.version) == "1.0.1" && !info.legacyInferred, "rehearsal: new ReDot.exe installed, manifest reports 1.0.1");
    CHECK(hexOf(install / "my-clips" / "clip1.mp4") == before["my-clips/clip1.mp4"] && hexOf(install / "notes.txt") == before["notes.txt"], "rehearsal: user files in the folder untouched");
    CHECK(runUpdate(o, net, rep).outcome == Outcome::UpToDate, "rehearsal: second run reports up to date");
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc >= 6 && std::wstring(argv[1]) == L"--rehearsal") return rehearsal(argv[2], argv[3], argv[4], argv[5]);
    auto t0 = std::chrono::steady_clock::now();
    fs::path work = argc > 1 ? fs::path(argv[1]) : fs::path(L"updater-test-work"), legacyExe, realZip;
    for (int i = 2; i + 1 < argc; ++i) { if (std::wstring(argv[i]) == L"--legacy-exe") legacyExe = argv[i + 1]; if (std::wstring(argv[i]) == L"--real-zip") realZip = argv[i + 1]; }
    std::error_code ec; fs::remove_all(work, ec); fs::create_directories(work);
    testVersions(); testFeed(); testDetection(work, legacyExe); testZipSafety(work); testUpdateAndRollback(work); testRunning(work);
    if (!realZip.empty()) testRealZip(work, realZip);
    printf("\n%d passed, %d failed in %.1f s\n", g_pass, g_fail, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return g_fail ? 1 : 0;
}
