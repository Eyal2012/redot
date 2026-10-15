#include "recording.h"
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <filesystem>
#include <iostream>

// Defined only in the verification target. Calls the production NVENC writer,
// AAC encoders and replay exporter; never opens WASAPI or plays generated PCM.
bool writeQuietMediaFixture(std::shared_ptr<GpuResources>, const std::wstring&, std::wstring&, bool matroska);
int wmain(int argc,wchar_t **argv) {
    if(argc<2||argc>3)return 2;const bool matroska=argc==3&&std::wstring(argv[2])==L"mkv";
    auto folder=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(folder/L"local");
    SetEnvironmentVariableW(L"LOCALAPPDATA",(folder/L"local").c_str());
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    auto gpu=std::make_shared<GpuResources>();ComPtr<IDXGIFactory1> factory;
    HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));ComPtr<IDXGIAdapter1> adapter;
    for(UINT i=0;SUCCEEDED(hr)&&factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){
        DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);
        if(desc.VendorId==0x10de){gpu->adapterName=desc.Description;gpu->adapterVendorId=desc.VendorId;break;}adapter.Reset();
    }
    if(!adapter){std::wcerr<<L"Missing NVIDIA adapter; no fixture was created.\n";CoUninitialize();return 1;}
    hr=D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&gpu->device,nullptr,&gpu->context);
    const char shader[]=R"(
        cbuffer Info:register(b0){float4 dimensions;}; Texture2D frameTexture:register(t0);SamplerState samplerState:register(s0);
        struct Out{float4 pos:SV_POSITION;float2 uv:TEXCOORD0;};
        Out VSMain(uint id:SV_VertexID){Out o;o.uv=float2(id==1?2:0,id==2?2:0);o.pos=float4(o.uv.x*2-1,1-o.uv.y*2,0,1);return o;}
        float4 PSMain(Out i):SV_TARGET{return frameTexture.Sample(samplerState,i.uv);}
    )";
    ComPtr<ID3DBlob> vs,ps,errors;
    if(SUCCEEDED(hr))hr=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"VSMain","vs_4_0",0,0,&vs,&errors);
    if(SUCCEEDED(hr))hr=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PSMain","ps_4_0",0,0,&ps,&errors);
    if(SUCCEEDED(hr))hr=gpu->device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&gpu->vertexShader);
    if(SUCCEEDED(hr))hr=gpu->device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&gpu->pixelShader);
    D3D11_BUFFER_DESC constant{};constant.ByteWidth=16;constant.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(SUCCEEDED(hr))hr=gpu->device->CreateBuffer(&constant,nullptr,&gpu->constants);
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
    if(SUCCEEDED(hr))hr=gpu->device->CreateSamplerState(&sampler,&gpu->sampler);
    std::wstring error;bool ok=SUCCEEDED(hr)&&writeQuietMediaFixture(gpu,folder.wstring(),error,matroska);
    std::wcout<<(ok?L"PASS":L"FAIL")<<L" deterministic GPU/NVENC/AAC ordinary and replay media; no WGC, WASAPI or audible playback: "<<error<<L"\n";
    gpu.reset();CoUninitialize();return ok?0:1;
}
