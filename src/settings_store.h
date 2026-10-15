#pragma once
#include "logging.h"
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <windows.h>

inline std::string settingsUtf8(const std::wstring &s)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n, nullptr, nullptr);
    return r;
}
inline std::wstring settingsWide(const std::string &s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n && !s.empty())
        throw std::runtime_error("UTF8");
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n);
    return r;
}
class SettingsStore
{
  public:
    using Values = std::map<std::string, std::wstring>;
    explicit SettingsStore(const std::wstring& filename = L"settings-v1.txt")
    {
        wchar_t folder[32768]{};
        GetEnvironmentVariableW(L"LOCALAPPDATA", folder, 32768);
        path_ = std::filesystem::path(folder) / L"PandaClip" / filename;
        worker_ = std::thread([this] { run(); });
    }
    ~SettingsStore()
    {
        {
            std::lock_guard l(mutex_);
            stopping_ = true;
        }
        wake_.notify_one();
        worker_.join();
    }
    Values load(std::wstring &status)
    {
        Values result;
        try
        {
            if (!std::filesystem::exists(path_))
                return result;
            if (std::filesystem::file_size(path_) > 65536)
                throw std::runtime_error("size");
            std::ifstream file(path_, std::ios::binary);
            std::string header, key, value;
            std::getline(file, header);
            if(!header.empty()&&header.back()=='\r')header.pop_back();
            if (header != "PandaClip settings 1")
                throw std::runtime_error("version");
            while (file >> std::quoted(key))
            {
                if (!(file >> std::quoted(value)) || key.size() > 64 || value.size() > 16000 || result.size() > 128)
                    throw std::runtime_error("syntax");
                result[key] = settingsWide(value);
            }
            if (!file.eof())
                throw std::runtime_error("read");
        }
        catch (const std::exception& e)
        {
            logLine(L"Settings load failed for %s: %s",path_.c_str(),settingsWide(e.what()).c_str());
            result.clear();
            status = L"Settings were unreadable or outdated; safe defaults loaded. Apply settings to replace them.";
        }
        return result;
    }
    void save(Values values)
    {
        std::lock_guard l(mutex_);
        if (values == last_ && (error_.empty() || std::chrono::steady_clock::now() < retryAfter_))
            return;
        last_ = values;
        pending_ = std::move(values);
        dirty_ = true;
        wake_.notify_one();
    }
    bool busy()
    {
        std::lock_guard l(mutex_);
        return dirty_ || writing_;
    }
    std::wstring error()
    {
        std::lock_guard l(mutex_);
        return error_;
    }

  private:
    void run()
    {
        for (;;)
        {
            Values values;
            {
                std::unique_lock l(mutex_);
                wake_.wait(l, [&] { return stopping_ || dirty_; });
                if (!dirty_ && stopping_)
                    break;
                values = std::move(pending_);
                dirty_ = false;
                writing_ = true;
            }
            std::wstring failure;
            try
            {
                std::filesystem::create_directories(path_.parent_path());
                auto temp = path_;
                temp += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
                std::ostringstream out;
                out << "PandaClip settings 1\n";
                for (auto &[k, v] : values)
                    out << std::quoted(k) << ' ' << std::quoted(settingsUtf8(v)) << '\n';
                auto data = out.str();
                HANDLE file =
                    CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE)
                    throw std::runtime_error("open");
                DWORD wrote{};
                bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &wrote, nullptr) &&
                          wrote == data.size() && FlushFileBuffers(file);
                CloseHandle(file);
                if (!ok ||
                    !MoveFileExW(temp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                {
                    DeleteFileW(temp.c_str());
                    throw std::runtime_error("replace");
                }
            }
            catch (...)
            {
                failure =
                    L"Could not save settings; previous settings file retained. Check the application settings folder (%LOCALAPPDATA%\\PandaClip).";
                logLine(L"%s", failure.c_str());
            }
            {
                std::lock_guard l(mutex_);
                writing_ = false;
                error_ = std::move(failure);
                retryAfter_ = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            }
        }
    }
    std::filesystem::path path_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    Values pending_, last_;
    bool stopping_{}, dirty_{}, writing_{};
    std::wstring error_;
    std::chrono::steady_clock::time_point retryAfter_{};
};
