// ReDotUpdater core: versions, GitHub release selection, signature verification, safe ZIP extraction and
// journaled install/rollback. No Qt, no third-party code. Network access is behind INet so tests can use files.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rdu {
namespace fs = std::filesystem;

constexpr int kUpdaterProtocol = 1;                       // package manifests may require a newer updater
inline const char* kReleaseUrlPrefix = "https://github.com/Eyal2012/redot/releases/download/";
inline const wchar_t* kReleasesApi = L"https://api.github.com/repos/Eyal2012/redot/releases?per_page=30";
inline const wchar_t* kWorkDirName = L".redot-update";    // the only folder the updater creates inside an install

std::wstring W(const std::string& utf8);
std::string U8(const std::wstring& wide);

// ---- versions -------------------------------------------------------------------------------------------
struct Version { int major = 0, minor = 0, patch = 0; std::string pre; };
bool parseVersion(std::string text, Version& out);          // "v1.0.1", "1.0.1-beta.2+build"; exactly 3 numeric parts
int compareVersions(const Version& a, const Version& b);    // semver ordering, -1/0/1
std::string versionString(const Version& v);

// ---- minimal JSON ---------------------------------------------------------------------------------------
struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false; double n = 0; std::string s;
    std::vector<Json> a; std::vector<std::pair<std::string, Json>> o;
    const Json* get(const char* key) const;
    std::string str(const char* key) const;
    bool flag(const char* key) const;
    double num(const char* key, double fallback = 0) const;
};
bool parseJson(const std::string& text, Json& out, std::string& err);

// ---- GitHub releases ------------------------------------------------------------------------------------
struct Asset { std::string name, url; uint64_t size = 0; };
struct Release { std::string tag; Version version; std::string notes; Asset zip, sig; };
enum class FeedStatus { Ok, NoStableRelease, MissingAssets, BadFeed };
struct FeedResult { FeedStatus status = FeedStatus::BadFeed; Release release; std::string detail; };
// Newest stable (non-draft, non-prerelease, no semver pre-release tag) release; it must carry the official
// ReDot-v<ver>-Windows-x64.zip and .sig assets hosted under the official repository URL.
FeedResult selectLatestRelease(const std::string& releasesJson);

// ---- crypto (Windows CNG) -------------------------------------------------------------------------------
using ProgressFn = std::function<bool(uint64_t done, uint64_t total)>;   // return false to cancel
bool sha256File(const fs::path& file, std::vector<uint8_t>& digest, const ProgressFn& progress, std::wstring& err);
// pub = BCRYPT_ECCPUBLIC_BLOB for ECDSA P-256, sig = 64-byte r||s over the SHA-256 digest.
bool verifyP256(const uint8_t* pub, size_t pubSize, const std::vector<uint8_t>& digest, const std::vector<uint8_t>& sig);
bool generateP256(std::vector<uint8_t>& privateBlob, std::vector<uint8_t>& publicBlob);
bool signDigest(const std::vector<uint8_t>& privateBlob, const std::vector<uint8_t>& digest, std::vector<uint8_t>& sig);

// ---- ZIP ------------------------------------------------------------------------------------------------
struct ZipEntry {
    std::string name;       // validated, forward slashes, container folder stripped (relative install path)
    uint16_t method = 0; uint32_t crc = 0, csize = 0, usize = 0; uint64_t dataOffset = 0; bool isDir = false;
};
struct ZipPackage { fs::path file; std::vector<ZipEntry> entries; uint64_t totalSize = 0; std::string strippedRoot; };
// Rejects: ZIP64/encrypted/unsupported methods, absolute or traversing or drive/stream/reserved names, symlinks,
// duplicates, reserved updater/user-data names, missing ReDot.exe or redot-version.json, oversized archives.
bool openZip(const fs::path& file, ZipPackage& pkg, std::wstring& err, bool requireManifest = true);
bool extractZip(const ZipPackage& pkg, const fs::path& stagingDir, const ProgressFn& progress, std::wstring& err);
bool validateEntryName(const std::string& rawName, std::string& normalized, std::wstring& why);

// ---- installation identification ------------------------------------------------------------------------
struct InstallInfo { fs::path dir; Version version; bool legacyInferred = false; };
enum class IdentStatus { Ok, NotReDot, Unrecognized };
// redot-version.json wins. Without it, only the public v1.0.0 portable layout (ReDot.exe reporting 0.3.0 plus
// THIRD-PARTY-NOTICES.txt) is accepted, and is treated as v1.0.0. Anything else is refused.
IdentStatus identifyInstall(const fs::path& dir, InstallInfo& info, std::wstring& why);
std::vector<fs::path> findInstallations(const std::vector<fs::path>& roots);
bool isReDotRunning(const fs::path& exePath);
std::vector<fs::path> runningReDotFolders();

// ---- install / rollback ---------------------------------------------------------------------------------
struct InstallOptions {
    fs::path installDir, stagingDir; std::string oldVersion;
    std::function<bool(size_t, const std::string&)> failHook;   // tests: return true to simulate a failure after item
    size_t crashAtIndex = static_cast<size_t>(-1);               // tests: stop after item without rolling back
};
bool installStaged(const InstallOptions& o, const std::vector<std::string>& rels, std::wstring& err);
enum class RecoverResult { Nothing, RolledBack, Failed };
RecoverResult recoverInterrupted(const fs::path& installDir, std::wstring& message);

// ---- network / orchestration ----------------------------------------------------------------------------
struct INet {
    virtual ~INet() = default;
    virtual bool getText(const std::wstring& url, std::string& body, std::wstring& err) = 0;
    virtual bool download(const std::wstring& url, const fs::path& dest, uint64_t expected, const ProgressFn& progress, std::wstring& err) = 0;
};
std::unique_ptr<INet> makeWinHttpNet();

struct Reporter {
    virtual ~Reporter() = default;
    virtual void status(const std::wstring& text) = 0;
    virtual void progress(double fraction) = 0;                  // 0..1, or <0 for indeterminate
    virtual bool waitForReDotClosed(const fs::path& exe) = 0;    // block until closed; false = user cancelled
};
struct Options {
    fs::path installDir;
    const uint8_t* publicKey = nullptr; size_t publicKeySize = 0;
    std::atomic<bool>* cancel = nullptr;
    bool checkOnly = false;
    std::wstring releasesUrl = kReleasesApi;
    std::function<bool(size_t, const std::string&)> failHook;    // tests only
    size_t crashAtIndex = static_cast<size_t>(-1);               // tests only
};
enum class Outcome { Updated, UpToDate, UpdateAvailable, Cancelled, Failed };
struct RunResult {
    Outcome outcome = Outcome::Failed;
    std::wstring message, recovery, notice;                      // notice: e.g. an interrupted update was rolled back
    std::string oldVersion, newVersion, notes; bool legacy = false;
};
RunResult runUpdate(const Options& o, INet& net, Reporter& rep);

}  // namespace rdu
