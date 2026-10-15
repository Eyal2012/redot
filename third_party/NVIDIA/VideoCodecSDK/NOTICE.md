`include/nvEncodeAPI.h` is NVIDIA's NVENC API header, version 13.1. It is mirrored by the FFmpeg nv-codec-headers project. The NVIDIA copyright notice and permissive license are preserved in the header.

Attribution/source: https://github.com/FFmpeg/nv-codec-headers/tree/master/include/ffnvcodec

PandaClip loads the NVIDIA display driver's `nvEncodeAPI64.dll` at runtime. No NVENC stub library or FFmpeg binary is linked into PandaClip.
