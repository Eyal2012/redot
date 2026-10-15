#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <windows.h>
// Optional metadata alongside a finalized MP4. Track IDs come from the muxer's
// stream IDs (MP4 uses one-based track IDs); a file-size guard rejects stale names.
inline void saveClipTrackNames(const std::wstring &path, const std::map<unsigned, std::string> &names) noexcept
{
    try
    {
        auto sidecar = std::filesystem::path(path + L".tracks");
        auto temp = std::filesystem::path(path + L".tracks.tmp");
        {
            std::ofstream out(temp, std::ios::binary);
            out << "PandaClip tracks 1 " << std::filesystem::file_size(path) << '\n';
            for (auto &[id, name] : names) {
                auto label=name;for(auto& c:label)if(c=='\n'||c=='\r'||c=='\0')c=' ';
                out << id << ' ' << label.substr(0,512) << '\n';
            }
            out.flush();
            if (!out)
                return;
        }
        MoveFileExW(temp.c_str(), sidecar.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    catch (...)
    {
    }
}
inline std::map<unsigned, std::wstring> loadClipTrackNames(const std::wstring &path)
{
    std::map<unsigned, std::wstring> result;
    try
    {
        auto meta = std::filesystem::path(path + L".tracks");
        if (!std::filesystem::exists(meta) || std::filesystem::file_size(meta) > 16384)
            return result;
        std::ifstream in(meta);
        std::string a, b;
        unsigned version{}, id{};
        uint64_t bytes{};
        if (!(in >> a >> b >> version >> bytes) || a != "PandaClip" || b != "tracks" || version != 1 ||
            bytes != std::filesystem::file_size(path))
            return result;
        std::string line;std::getline(in,line);
        while (std::getline(in,line))
        {
            if (result.size() >= 17)
                return {};
            std::istringstream row(line);std::string name;if(!(row>>id)||!id||id>32)return {};std::getline(row,name);if(!name.empty()&&name.front()==' ')name.erase(0,1);if(name.empty()||name.size()>512)return {};
            auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),int(name.size()),nullptr,0);if(!n)return {};std::wstring label(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),int(name.size()),label.data(),n);result[id]=std::move(label);
        }
    }
    catch (...)
    {
    }
    return result;
}
