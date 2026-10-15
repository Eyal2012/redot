#pragma once
#include <windows.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <array>
inline float readPcm(const BYTE* data, size_t frame, unsigned channel, const WAVEFORMATEX* format) {
    const unsigned channels = std::max<unsigned>(1, format->nChannels);
    const unsigned ch = std::min(channel, channels - 1);
    const bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    const unsigned bits = format->wBitsPerSample;
    const BYTE* sample = data + frame * format->nBlockAlign + ch * ((bits + 7) / 8);
    if (isFloat && bits == 32) { float v; CopyMemory(&v, sample, 4); return std::isfinite(v) ? v : 0.0f; }
    if (isFloat && bits == 64) { double v; CopyMemory(&v, sample, 8); return std::isfinite(v) ? static_cast<float>(v) : 0.0f; }
    if (bits == 8) return (static_cast<int>(*sample) - 128) / 128.0f;
    if (bits == 16) { int16_t v; CopyMemory(&v, sample, 2); return static_cast<float>(v / 32768.0); }
    if (bits == 24) { int32_t v = sample[0] | (sample[1] << 8) | (sample[2] << 16); if (v & 0x800000) v |= ~0xffffff; return static_cast<float>(v / 8388608.0); }
    if (bits == 32) { int32_t v; CopyMemory(&v, sample, 4); return static_cast<float>(v / 2147483648.0); }
    return 0.0f;
}

// Extensible channel masks enumerate channels in ascending speaker-bit order.
// Conventional unmasked PCM layout is only used when no mask is supplied.
inline std::array<float,2> stereoPcm(const BYTE* data,size_t frame,const WAVEFORMATEX* f){
    if(f->nChannels==1){const float v=readPcm(data,frame,0,f);return {v,v};}
    if(f->nChannels==2)return {readPcm(data,frame,0,f),readPcm(data,frame,1,f)};
    DWORD mask=0;
    if(f->wFormatTag==WAVE_FORMAT_EXTENSIBLE && f->cbSize>=sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX))mask=reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f)->dwChannelMask;
    if(!mask){
        if(f->nChannels==3)mask=SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER;
        else if(f->nChannels==4)mask=SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT;
        else if(f->nChannels==6)mask=KSAUDIO_SPEAKER_5POINT1;
        else if(f->nChannels==8)mask=KSAUDIO_SPEAKER_7POINT1_SURROUND;
    }
    float left=0,right=0;unsigned ch=0;
    for(unsigned bit=0;bit<32 && ch<f->nChannels;++bit){const DWORD speaker=DWORD(1)<<bit;if(!(mask&speaker))continue;
        const float value=readPcm(data,frame,ch++,f);
        if(speaker==SPEAKER_FRONT_LEFT)left+=value;
        else if(speaker==SPEAKER_FRONT_RIGHT)right+=value;
        else if(speaker==SPEAKER_LOW_FREQUENCY){left+=value*0.25f;right+=value*0.25f;}
        else if(speaker==SPEAKER_FRONT_CENTER || speaker==SPEAKER_BACK_CENTER){left+=value*0.7071f;right+=value*0.7071f;}
        else if(speaker==SPEAKER_BACK_LEFT || speaker==SPEAKER_SIDE_LEFT || speaker==SPEAKER_FRONT_LEFT_OF_CENTER)left+=value*0.7071f;
        else if(speaker==SPEAKER_BACK_RIGHT || speaker==SPEAKER_SIDE_RIGHT || speaker==SPEAKER_FRONT_RIGHT_OF_CENTER)right+=value*0.7071f;
    }
    // Unsupported/unmasked channel layout: retain front pair, never guess a
    // center/LFE index. Capture logs the native channel mask for diagnosis.
    if(ch!=f->nChannels)return {readPcm(data,frame,0,f),readPcm(data,frame,1,f)};
    return {left,right};
}
