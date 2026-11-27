/* SPDX-License-Identifier: MIT */
#include <gameshare/guest_display.hpp>
#include "runtime_state.hpp"
#include <switch.h>
#include <mutex>

namespace gameshare {
namespace {
std::uint32_t badInput() { return MAKERESULT(Module_Libnx, LibnxError_BadInput); }
std::uint32_t notOpen() { return MAKERESULT(Module_Libnx, LibnxError_NotInitialized); }
}
struct NativeGuestDisplay::Impl {
    explicit Impl(const RendererHooks& value) : hooks(value) {}
    RendererHooks hooks;
    mutable std::mutex mutex;
    Vi2Display display{};
    Vi2Endpoint endpoint{};
    Vi2Layer layer{};
    bool vi2 = false, displayOpen = false, endpointOpen = false;
    bool layerCreated = false, attached = false, selected = false;
    bool rendererAttempted = false, ready = false;
    std::uint32_t openError = 0, closeError = 0;

    bool resources() const {
        return vi2 || displayOpen || endpointOpen || layerCreated || attached ||
               selected || rendererAttempted;
    }
    std::uint32_t failOpen(std::uint32_t rc) { openError = rc; return rc; }
    std::uint32_t failClose(std::uint32_t rc) {
        closeError = rc;
        detail::restartRequired = true;
        return rc;
    }
    std::uint32_t retireLocked() {
        if (closeError) return closeError;
        ready = false;
        if (rendererAttempted) {
            std::uint32_t rc;
            try { rc = hooks.close(hooks.user); }
            catch (...) { rc = badInput(); }
            if (rc) return failClose(rc);
            rendererAttempted = false;
        }
        return 0;
    }
};

NativeGuestDisplay::NativeGuestDisplay(const RendererHooks& hooks)
    : impl(std::make_unique<Impl>(hooks)) {}
NativeGuestDisplay::~NativeGuestDisplay() {
    // Dropping a live endpoint would also invalidate its display pointer.
    // Preserve the complete allocation on failure, even outside Host's normal
    // process-quarantine ownership. Recovery is title/process closure.
    try {
        if (close() != 0) (void)impl.release();
    } catch (...) {
        detail::restartRequired = true;
        (void)impl.release();
    }
}
std::uint32_t NativeGuestDisplay::open() {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->closeError) return impl->closeError;
    if (impl->ready) return 0;
    if (impl->openError) return impl->openError;
    if (impl->resources()) return badInput(); // Retired display awaits close.
    if ((impl->hooks.open == nullptr) != (impl->hooks.close == nullptr))
        return impl->failOpen(badInput());
    auto rc = vi2Initialize();
    if (rc) return impl->failOpen(rc);
    impl->vi2 = true;
    rc = vi2OpenDisplay(&impl->display, "player1");
    if (rc) return impl->failOpen(rc);
    impl->displayOpen = true;
    vi2CreateEndpoint(&impl->endpoint, &impl->display, "default");
    impl->endpointOpen = true;
    rc = vi2CreateLayer(&impl->layer);
    if (rc) return impl->failOpen(rc);
    impl->layerCreated = true;
    rc = vi2AttachLayer(&impl->layer, &impl->display);
    if (rc) return impl->failOpen(rc);
    impl->attached = true;
    rc = vi2SelectLayer(&impl->layer, &impl->endpoint);
    if (rc) return impl->failOpen(rc);
    impl->selected = true;
    if (impl->hooks.open) {
        impl->rendererAttempted = true;
        try { rc = impl->hooks.open(impl->hooks.user, &impl->layer); }
        catch (...) { rc = badInput(); }
        if (rc) return impl->failOpen(rc);
    }
    impl->ready = true;
    return 0;
}
std::uint32_t NativeGuestDisplay::retire() {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->retireLocked();
}
std::uint32_t NativeGuestDisplay::close() {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (const auto rc = impl->retireLocked()) return rc;
    if (impl->attached) {
        const auto rc = vi2DetachLayer(&impl->layer);
        if (rc) return impl->failClose(rc);
        impl->attached = false;
    }
    if (impl->selected) {
        const auto rc = vi2ClearLayerSelection(&impl->endpoint);
        if (rc) return impl->failClose(rc);
        impl->selected = false;
    }
    if (impl->layerCreated) {
        const auto rc = vi2CloseLayer(&impl->layer);
        if (rc) return impl->failClose(rc);
        impl->layerCreated = false;
    }
    if (impl->endpointOpen) {
        vi2CloseEndpoint(&impl->endpoint);
        impl->endpointOpen = false;
    }
    if (impl->displayOpen) {
        const auto rc = vi2CloseDisplay(&impl->display);
        if (rc) return impl->failClose(rc);
        impl->displayOpen = false;
    }
    if (impl->vi2) {
        vi2Exit();
        impl->vi2 = false;
    }
    impl->openError = 0;
    return 0;
}
std::uint32_t NativeGuestDisplay::withLayer(
    std::uint32_t (*draw)(void*, Vi2Layer*), void* user) {
    if (!draw) return badInput();
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->closeError) return impl->closeError;
    if (!impl->ready) return notOpen();
    try { return draw(user, &impl->layer); }
    catch (...) { return badInput(); }
}
bool NativeGuestDisplay::isOpen() const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->ready;
}
bool NativeGuestDisplay::hasResources() const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->resources();
}
} // namespace gameshare
