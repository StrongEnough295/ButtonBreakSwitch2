/* SPDX-License-Identifier: MIT */
#pragma once
#include "host.hpp"
#include <cstddef>
#include <memory>

namespace gameshare {
// Optional fixed1280x720 RGBA8 uploader for a libounce CPU-console application.
// It borrows an ALREADY live consoleInit CPU framebuffer's GPU/sync/client and
// worker-slot0 event through a private ABI pinned to libounce
// d1718bb7076fb356cb4cb81ab13d035db5367eea. It is not a Vulkan/EGL renderer.
// Keep that console context alive and keep other slot0/VM/range operations idle
// until Host::canUnload() is true. Ordinary consoleUpdate can continue.
// A different CPU backend/SDK requires ABI requalification before use.
class CpuImageStreamer final : public VideoTarget {
public:
    static constexpr std::uint32_t Width=1280, Height=720;
    CpuImageStreamer();
    ~CpuImageStreamer() override;
    CpuImageStreamer(const CpuImageStreamer&)=delete;
    CpuImageStreamer& operator=(const CpuImageStreamer&)=delete;
    std::uint32_t open() override;
    // Retire images before native Finalize; keep VI2 layer/display live.
    std::uint32_t retire() override;
    std::uint32_t close() override;
    // Copies RGBA bytes synchronously; source only needs to live for this call.
    // stride is bytes per source row and must be at leastWidth*4. Exactly Width
    // and Height are supported. The caller owns a readable span of
    // (Height-1)*stride+Width*4 bytes. Release/slot waits are each bounded at2s;
    // synchronous native IPC itself has no timeout. Serialized against close.
    std::uint32_t presentRgba8(const void* source,std::size_t stride,
        std::uint32_t width=Width,std::uint32_t height=Height);
    bool isOpen() const;
    bool hasResources() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace gameshare
