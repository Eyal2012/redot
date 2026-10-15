// WinHTTP transport. HTTPS only, official GitHub hosts only; authenticity never depends on the transport
// (packages are verified against the embedded signing key regardless).
#include "core.h"
#include <winhttp.h>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

namespace rdu {
namespace {

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET v = nullptr) : h(v) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete; Handle& operator=(const Handle&) = delete;
};

std::wstring lastError(const wchar_t* what) {
    DWORD e = GetLastError();
    switch (e) {
    case ERROR_WINHTTP_TIMEOUT: return std::wstring(what) + L": the connection timed out.";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: case ERROR_WINHTTP_CANNOT_CONNECT: case ERROR_WINHTTP_CONNECTION_ERROR:
        return std::wstring(what) + L": cannot connect (are you offline?).";
    case ERROR_WINHTTP_SECURE_FAILURE: return std::wstring(what) + L": the secure connection failed (check your system date and proxy).";
    default: return std::wstring(what) + L" (Windows error " + std::to_wstring(e) + L").";
    }
}

class WinHttpNet final : public INet {
public:
    bool getText(const std::wstring& url, std::string& body, std::wstring& err) override {
        body.clear();
        return fetch(url, 0, [&](const uint8_t* p, size_t n) { if (body.size() + n > (8u << 20)) return false; body.append(reinterpret_cast<const char*>(p), n); return true; }, nullptr, err);
    }
    bool download(const std::wstring& url, const fs::path& dest, uint64_t expected, const ProgressFn& progress, std::wstring& err) override {
        std::error_code ec; fs::remove(dest, ec);
        HANDLE out = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE) { err = L"Cannot create the download file."; return false; }
        bool ok = fetch(url, expected, [&](const uint8_t* p, size_t n) { DWORD w = 0; return WriteFile(out, p, static_cast<DWORD>(n), &w, nullptr) && w == n; }, progress, err);
        CloseHandle(out);
        if (!ok) fs::remove(dest, ec);
        return ok;
    }
private:
    template <class Sink>
    bool fetch(const std::wstring& url, uint64_t expected, Sink sink, const ProgressFn& progress, std::wstring& err) {
        URL_COMPONENTS uc{}; uc.dwStructSize = sizeof(uc);
        wchar_t host[256]{}, path[4096]{};
        uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096; uc.dwExtraInfoLength = static_cast<DWORD>(-1);
        std::wstring extra;
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) { err = L"Refusing a non-HTTPS address."; return false; }
        std::wstring h = host;
        if (_wcsicmp(h.c_str(), L"api.github.com") != 0 && _wcsicmp(h.c_str(), L"github.com") != 0) { err = L"Refusing a non-GitHub address."; return false; }
        std::wstring objectName = std::wstring(path, uc.dwUrlPathLength);
        if (uc.lpszExtraInfo && uc.dwExtraInfoLength) objectName.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

        Handle session(WinHttpOpen(L"ReDotUpdater/1.0 (+https://github.com/Eyal2012/redot)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0));
        if (!session.h) session.h = WinHttpOpen(L"ReDotUpdater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
        if (!session.h) { err = lastError(L"Cannot start networking"); return false; }
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
        WinHttpSetTimeouts(session.h, 10000, 15000, 30000, 30000);
        Handle conn(WinHttpConnect(session.h, h.c_str(), uc.nPort, 0));
        if (!conn.h) { err = lastError(L"Cannot connect to GitHub"); return false; }
        Handle req(WinHttpOpenRequest(conn.h, L"GET", objectName.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        if (!req.h) { err = lastError(L"Cannot create the request"); return false; }
        static const wchar_t* headers = L"Accept: application/vnd.github+json, application/octet-stream\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        if (!WinHttpSendRequest(req.h, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(req.h, nullptr)) { err = lastError(L"GitHub did not respond"); return false; }
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status != 200) {
            err = L"GitHub answered HTTP " + std::to_wstring(status) + (status == 403 || status == 429 ? L" (rate limit reached; try again later)." : status == 404 ? L" (not found)." : L".");
            return false;
        }
        wchar_t len[32]{}; sz = sizeof(len);
        uint64_t total = expected;
        if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, len, &sz, WINHTTP_NO_HEADER_INDEX)) total = _wcstoui64(len, nullptr, 10);
        std::vector<uint8_t> buf(64 * 1024); uint64_t done = 0;
        for (;;) {
            DWORD got = 0;
            if (!WinHttpReadData(req.h, buf.data(), static_cast<DWORD>(buf.size()), &got)) { err = lastError(L"The download was interrupted"); return false; }
            if (!got) break;
            if (!sink(buf.data(), got)) { err = L"Could not store the downloaded data."; return false; }
            done += got;
            if (progress && !progress(done, total)) { err = L"Cancelled."; return false; }
        }
        if (total && done != total) { err = L"The download ended early."; return false; }
        return true;
    }
};

}  // namespace

std::unique_ptr<INet> makeWinHttpNet() { return std::make_unique<WinHttpNet>(); }

}  // namespace rdu
