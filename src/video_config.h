#pragma once
#include <cstdint>
#include <string>

// Ordinary-recording container. Instant-replay exports are always MP4 regardless of this setting.
enum class ContainerFormat : unsigned
{
    Mkv = 0, // default: incrementally written Matroska, recoverable after an unexpected exit
    Mp4 = 1
};
inline ContainerFormat containerFromSetting(unsigned value)
{
    return value == 1 ? ContainerFormat::Mp4 : ContainerFormat::Mkv; // any invalid value falls back to MKV
}

struct VideoConfig
{
    ContainerFormat container{ContainerFormat::Mkv};
    unsigned width{1920}, height{1080}, fps{60};
    unsigned rateMode{0}; // 0 VBR, 1 CBR, 2 CQP
    unsigned bitrateMbps{20}, maxBitrateMbps{35}, qp{23};
    bool trackMix{true}, trackGame{true}, trackMicrophone{true};
    std::wstring description() const
    {
        return std::to_wstring(width) + L"x" + std::to_wstring(height) + L" H.264 at " + std::to_wstring(fps) + L" FPS";
    }
    bool validate(std::wstring &error) const
    {
        if (width < 320 || width > 3840 || height < 180 || height > 2160 || width % 2 || height % 2)
            error = L"Use even dimensions: width 320-3840, height 180-2160. NVIDIA capabilities are checked at session "
                    L"startup.";
        else if (fps != 30 && fps != 60 && fps != 120)
            error = L"Output FPS must be 30, 60, or 120. Distinct capture frames may be lower.";
        else if (rateMode > 2 || qp > 51 || bitrateMbps < 1 || bitrateMbps > 200 ||
                 (rateMode == 0 && maxBitrateMbps < bitrateMbps) || maxBitrateMbps > 200)
            error = L"Rate control: bitrate 1-200 Mbps, VBR maximum >= bitrate, CQP 0-51 (lower is higher quality).";
        else
            return true;
        return false;
    }
};
