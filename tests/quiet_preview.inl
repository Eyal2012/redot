// Test-only readback of the canonical scene presentation.
public:
bool quietReadback(std::vector<uint32_t> &pixels, unsigned &width, unsigned &height)
{
    if (!swapChain_)
        return false;
    std::lock_guard lock(*gpuResources_->contextMutex);
    ComPtr<ID3D11Texture2D> buffer, staging;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&buffer))))
        return false;
    D3D11_TEXTURE2D_DESC desc{};
    buffer->GetDesc(&desc);
    width = desc.Width;
    height = desc.Height;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(d3dDevice_->CreateTexture2D(&desc, nullptr, &staging)))
        return false;
    immediate_->CopyResource(staging.Get(), buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(immediate_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    pixels.resize(size_t(width) * height);
    for (unsigned y = 0; y < height; ++y)
        memcpy(pixels.data() + size_t(y) * width, static_cast<uint8_t *>(mapped.pData) + y * mapped.RowPitch,
               width * 4);
    immediate_->Unmap(staging.Get(), 0);
    for (auto &pixel : pixels)
        pixel |= 0xff000000;
    return true;
}

private:
