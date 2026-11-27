# Rendering a separate guest image

`gameshare::VideoMode::Mirror` captures the application's default display. `SeparateDisplay` selects the native mode 1 route and renders through a `VideoTarget` supplied in `HostConfig::videoTarget`. Both paths host one local guest. Moonlight continues to use the default mirroring mode.

The SDK includes [`NativeGuestDisplay`](../include/gameshare/guest_display.hpp), a `VideoTarget` that owns the guest's native `player1` VI2 display and layer. It supplies renderer hooks and serialized layer access, so an application can draw an image that differs from its local screen.

The host owns the sequence of three target phases, each running on its native worker and returning a native Result (zero means success):

| Target method | Phase |
| --- | --- |
| `open()` | Prepare display and rendering resources. |
| `retire()` | Stop new draws and retire submitted images **before** StreamPlay Finalize; preserve the layer/display. |
| `close()` | Release the layer/display **after** successful Finalize. |

Both cleanup methods must tolerate partial initialization after a failed `open()`. A failed retirement retains resources and stops cleanup before Finalize; a failed closure retains remaining resources after Finalize. Either failure makes ordinary unload unsafe. Let `Host` call these methods; do not retire or close the target independently while it is hosting.

## Fixed RGBA8 image uploader

[`CpuImageStreamer`](../include/gameshare/cpu_image_streamer.hpp) is a ready-made `VideoTarget` for a fixed 1280×720 RGBA8 image. It requires an **already initialized libounce CPU-console framebuffer**, such as one created by `consoleInit()`. It borrows that context's GPU, synchronization/client resources and worker-slot-0 event. Ordinary `consoleUpdate()` rendering may continue.

Build the SDK with `GAMESHARE_ENABLE_CPU_STREAMER=ON` to include this helper. The CMake target exports `GAMESHARE_HAS_CPU_STREAMER=1` in that configuration, or `0` when the helper is omitted. Guard application references with that definition when supporting both builds. `Host` and `NativeGuestDisplay` remain available when the CPU helper is disabled.

This helper uses the private CPU-window ABI pinned to libounce revision [`d1718bb7076fb356cb4cb81ab13d035db5367eea`](https://github.com/ouncebrew/libounce/tree/d1718bb7076fb356cb4cb81ab13d035db5367eea). A different CPU backend or SDK revision requires requalification. It is not a Vulkan/EGL adapter and must not be attached to an unrelated graphics context. In particular, enabling it in Moonlight's existing Vulkan renderer is not qualified by the CPU-console implementation.

```cpp
#include <gameshare/cpu_image_streamer.hpp>

// After consoleInit() and the normal application lifecycle are ready:
auto guestImages = std::make_shared<gameshare::CpuImageStreamer>();
gameshare::HostConfig config;
config.videoMode = gameshare::VideoMode::SeparateDisplay;
config.videoTarget = guestImages;
gameshare::Host host(config);
host.start();

// During the application loop, rgba points to your readable RGBA8 image:
if (host.state() == gameshare::Host::State::Hosting) {
    const std::uint32_t result = guestImages->presentRgba8(rgba, rowStride);
    // Handle nonzero result outside the SDK callback/lifecycle code.
}
```

The fragment assumes application-owned `rgba` and `rowStride` values. `rowStride` is measured in bytes and must be at least `1280 * 4`; the readable span must cover `(720 - 1) * rowStride + 1280 * 4` bytes. `presentRgba8()` accepts only the declared fixed dimensions, copies the source synchronously, and serializes against retirement and closure. The source may be reused or released after the call returns. The SDK owns the copied image's backing and presentation lifetime. Its `retire()` retires those images while keeping its VI2 layer/display alive for Finalize; `close()` releases them afterwards.

Each release/worker-slot wait is bounded at two seconds; synchronous native IPC itself has no timeout. Keep presentation work off a latency-sensitive UI thread if this wait is unacceptable. A `Hosting` check can race shutdown, so handle a nonzero presentation result normally.

Keep the original console/GPU/synchronization/client context alive until `host.stopAndWait()` has returned **and** `host.canUnload()` is true. Do not perform competing worker-slot-0, VM or range operations while the helper owns its resources. A retained cleanup failure requires keeping these dependencies alive and closing the full hosting title/process through HOME, rather than returning to hbmenu.

## Custom renderer hooks

```cpp
#include <gameshare/guest_display.hpp>

// Application implementations, returning a native Result (zero = success):
std::uint32_t openRenderer(void* user, Vi2Layer* layer);
std::uint32_t retireRenderer(void* user);
std::uint32_t drawGuestImage(void* user, Vi2Layer* layer);

// During application setup, with shared_ptr rendererContext already initialized:
gameshare::RendererHooks hooks;
hooks.user = rendererContext.get();
hooks.open = openRenderer;
hooks.close = retireRenderer;
hooks.owner = rendererContext;
auto guestDisplay = std::make_shared<gameshare::NativeGuestDisplay>(hooks);

gameshare::HostConfig config;
config.videoMode = gameshare::VideoMode::SeparateDisplay;
config.videoTarget = guestDisplay;
gameshare::Host host(config);
```

This is an integration fragment. `rendererContext` and the three functions are supplied by the application; they are not SDK types or an encoded-media decoder. The optional `owner` keeps the renderer context alive through normal operation and retained failure. A raw `user` pointer alone does not provide ownership.

| Callback | Required behavior |
| --- | --- |
| `RendererHooks::open(void*, Vi2Layer*)` | Create the renderer's own image/backing/registration resources for the supplied layer. Return zero only after preparation succeeds. |
| `RendererHooks::close(void*)` | Renderer-retirement hook called by target `retire()` before Finalize. Stop new submissions, retire all submitted images, then release the renderer's own resources. Handle partial initialization too; do not destroy the target's VI2 layer/display. |
| `withLayer(draw, user)` callback | Perform a bounded render/present operation using the borrowed layer. Return the native Result. |

The open/close hooks run on the host worker while the target's drawing mutex is held. The hook's `close` name refers to renderer retirement, not the later target `close()` phase. `withLayer()` holds that same mutex on its caller's thread, preventing target retirement or closure during the callback. Do not throw, recursively call the target, or call host lifecycle methods from a hook or draw callback.

After native streaming has started, submit guest frames through serialized access:

```cpp
if (host.state() == gameshare::Host::State::Hosting) {
    const std::uint32_t result =
        guestDisplay->withLayer(drawGuestImage, rendererContext.get());
    // Handle a nonzero result after this callback has returned.
}
```

Target retirement or closure may race the state check. The serialized operation returns a failure rather than authorizing access after retirement. `isOpen()` becomes false when retired even though `hasResources()` remains true until the layer/display are released. Do not retain its borrowed `Vi2Layer*` or bypass `withLayer()` for later draws.

## Image ownership and completion

Serializing a callback does not prove GPU completion. The renderer must track its producer/presentation dependencies and wait for image retirement before releasing backing memory. Its `RendererHooks::close` callback must complete this retirement before returning success to target `retire()`. The native layer/display remain alive until target `close()` after Finalize. `NativeGuestDisplay::close()` also invokes retirement idempotently before releasing those resources. A callback that returns an error after uncertain retirement must preserve resources that a native consumer might still use.

The adapter owns VI2 display/layer resources; the renderer owns its images, backing, memory registrations and any graphics handles it created. Application GPU/synchronization handles that it borrows remain owned by the application. `RendererHooks::owner` can retain the renderer context, but it cannot retain dependencies that context only refers to through raw pointers or handles. Keep those dependencies alive through successful target closure. If cleanup fails, keep them alive until the entire hosting title/process is closed.

The prior Probe's typed RGBA8 renderer demonstrated this route with a stock guest. A legacy default-window framebuffer is not a substitute for a guest-layer renderer: the existing console framebuffer helper accepts the default window, and unrelated window/layer structs must not be copied or cast into one another.

## Media boundary

The separate target is an image/rendering interface. Applications may decode their own video and draw the resulting pixels or graphics into that target. No compressed H.264/AV1 packet submission, encoded-audio transport, generic FFmpeg `AVFrame` import or generic Vulkan-image import has been recovered by this SDK.

Audio sharing remains application-mix sharing, configured independently of the selected video mode. See [integration](INTEGRATION.md) and [safe shutdown](LIFECYCLE.md).
