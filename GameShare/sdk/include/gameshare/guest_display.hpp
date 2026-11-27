/* SPDX-License-Identifier: MIT */
#pragma once
#include "host.hpp"
extern "C" {
#include <switch2/services/vi2.h>
}
#include <memory>

namespace gameshare {
// Callbacks execute while the target's drawing mutex is held. They must not
// recursively call this target. Exceptions become a nonzero Result; a close
// exception quarantines the target. A failed open can have allocated GPU
// resources, so close is called even when open returned an error. Keep user
// and all renderer resources alive until close succeeds or the title exits.
struct RendererHooks {
    void* user = nullptr;
    std::uint32_t (*open)(void*, Vi2Layer*) = nullptr;
    std::uint32_t (*close)(void*) = nullptr;
    // Optional strong owner for user/renderer lifetime, also kept if shutdown
    // quarantines the target. A raw user pointer alone remains caller-owned.
    std::shared_ptr<void> owner;
};

// One separate native guest display (player1, endpoint default). This owns its
// VI2 reference, display and layer, and never uses NRO offsets or a second
// graphics root. The renderer owns image registration and GPU resources.
class NativeGuestDisplay final : public VideoTarget {
public:
    explicit NativeGuestDisplay(const RendererHooks& hooks = {});
    ~NativeGuestDisplay() override;
    NativeGuestDisplay(const NativeGuestDisplay&) = delete;
    NativeGuestDisplay& operator=(const NativeGuestDisplay&) = delete;

    std::uint32_t open() override;
    // Stops drawing and retires renderer resources, keeping the VI2 layer and
    // display alive until native StreamPlay Finalize has succeeded.
    std::uint32_t retire() override;
    // After Finalize, retires if needed, then detaches/clears/closes the layer.
    // A failure quarantines this target: later calls return the first failure
    // without repeating a destructive operation. Host retains the target.
    std::uint32_t close() override;

    // Serializes drawing against close. The layer pointer is borrowed only for
    // this callback; do not retain it. GPU-backed draws require RendererHooks
    // whose close callback retires every image before the layer can disappear.
    std::uint32_t withLayer(std::uint32_t (*draw)(void*, Vi2Layer*), void* user);
    bool isOpen() const;
    bool hasResources() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace gameshare
