#pragma once
#include "../src/recording.h"
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <d3d11_4.h>
inline std::shared_ptr<GpuResources> makeQuietGpu(){
    auto gpu=std::make_shared<GpuResources>();ComPtr<IDXGIFactory1> factory;ComPtr<IDXGIAdapter1> adapter;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return {};
    for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);if(d.VendorId==0x10de){gpu->adapterName=d.Description;gpu->adapterVendorId=d.VendorId;break;}adapter.Reset();}
    if(!adapter)return {};
    HRESULT hr=D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&gpu->device,nullptr,&gpu->context);
    ComPtr<ID3D11Multithread> safe; if(gpu->context && SUCCEEDED(gpu->context.As(&safe)))safe->SetMultithreadProtected(TRUE);
    const char shader[]=R"(cbuffer Info:register(b0){float4 dimensions;};Texture2D image:register(t0);SamplerState samplerState:register(s0);struct Out{float4 pos:SV_POSITION;float2 uv:TEXCOORD0;};Out VSMain(uint id:SV_VertexID){Out o;o.uv=float2(id==1?2:0,id==2?2:0);o.pos=float4(o.uv.x*2-1,1-o.uv.y*2,0,1);return o;}float4 PSMain(Out i):SV_TARGET{return image.Sample(samplerState,i.uv);})";
    ComPtr<ID3DBlob> vs,ps,errors;
    if(SUCCEEDED(hr))hr=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"VSMain","vs_4_0",0,0,&vs,&errors);
    if(SUCCEEDED(hr))hr=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PSMain","ps_4_0",0,0,&ps,&errors);
    if(SUCCEEDED(hr))hr=gpu->device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&gpu->vertexShader);
    if(SUCCEEDED(hr))hr=gpu->device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&gpu->pixelShader);
    D3D11_BUFFER_DESC c{};c.ByteWidth=16;c.BindFlags=D3D11_BIND_CONSTANT_BUFFER;if(SUCCEEDED(hr))hr=gpu->device->CreateBuffer(&c,nullptr,&gpu->constants);
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
    if(SUCCEEDED(hr))hr=gpu->device->CreateSamplerState(&sampler,&gpu->sampler);
    return SUCCEEDED(hr)?gpu:std::shared_ptr<GpuResources>{};
}
