# Application integration

The public entry points are in [`gameshare/host.hpp`](../include/gameshare/host.hpp) and [`gameshare/controller.hpp`](../include/gameshare/controller.hpp). The host copies its configuration at construction. Configure a new object after the previous host has fully stopped if different settings are needed.

## Configuration

| Field | Default | Meaning |
| --- | --- | --- |
| `videoMode` | `gameshare::VideoMode::Mirror` | Mirror the default display, or select `SeparateDisplay` for an application-rendered guest image. |
| `videoTarget` | empty | `std::shared_ptr<gameshare::VideoTarget>` required in separate-display mode. |
| `mirrorAudio` | `true` | Request native sharing of the application's audio mix. Audible output still needs fixture qualification. |
| `guestAudio` | `nullptr` | Optional direct PCM callback for separate display. Requires `mirrorAudio=false`. |
| `readGuestInput` | `true` | Read guest controller shared memory and publish snapshots through `controller()`. |
| `supportedControllerStyles` | `7` | Nonzero subset of the native style bits FullKey `1`, Handheld `2`, JoyDual `4`. These are the decoded styles. |
| `matchTimeoutMs` | `240000` | Deadline for each foreground user-selection or matchmaking wait, in the range `1000`–`600000` ms. Cancellation also observes `requestStop()`. |
| `log` | `nullptr` | Optional `void (*)(void*, gameshare::LogLevel, const char*)` log sink. |
| `logUser` | `nullptr` | Application-owned context passed as the callback's first argument. |

Unsupported style bits, an empty style mask, or a timeout outside that range are rejected. A different style bit does not add its controller-ring decoder. Configuration is copied at construction; changing the caller's struct afterwards has no effect.

The log callback runs on the native worker thread. Keep it short, do not throw, and do not call host lifecycle methods or UI APIs from it. Copy the message before retaining it asynchronously: the pointer is valid only during the callback. Keep `logUser` alive until `stopAndWait()` or the host destructor has returned.

```cpp
static void writeGameShareLog(void* user, gameshare::LogLevel level,
                              const char* message)
{
    // Copy into the application's thread-safe log queue.
    auto* queue = static_cast<MyLogQueue*>(user);
    queue->push(level, std::string(message));
}

gameshare::HostConfig config;
config.log = writeGameShareLog;
config.logUser = &logQueue;
gameshare::Host host(config);
```

`MyLogQueue` is an application-provided type. No particular logging framework is required.

## Mirror or render a separate guest image

Initialize the application's normal applet lifecycle and graphics environment before starting the host. Both paths use native local type 0. `gameshare::VideoMode::Mirror` selects native mode 0 and mirrors the default display; keep rendering that display normally. This remains the default and Moonlight's integration choice.

`gameshare::VideoMode::SeparateDisplay` selects native mode 1 and requires a configured `VideoTarget`. That path renders an image specifically for the guest instead of copying the default screen. The host retains a shared reference to its target and coordinates its opening, retirement and closure with native initialization and shutdown.

All three `VideoTarget` methods run on the native worker and return a `std::uint32_t` native Result: zero means success.

| Method | Required behavior |
| --- | --- |
| `open()` | Prepare the guest display and renderer. |
| `retire()` | Stop new draws and retire every submitted image, while keeping the native layer/display alive. Runs before StreamPlay Finalize. |
| `close()` | Release the layer/display after successful StreamPlay Finalize. |

Opening is marked as attempted before the callback, so both cleanup phases must handle a partially failed `open()`. If retirement fails, cleanup stops before Finalize and preserves the target. If closure fails after Finalize, remaining target resources are retained. Either failure quarantines the process and prevents normal unload. Let `Host` drive these phases; do not close a target underneath an active host. Do not throw or call host lifecycle methods from the callbacks.

For an application-defined target:

```cpp
gameshare::HostConfig config;
config.videoMode = gameshare::VideoMode::SeparateDisplay;
config.videoTarget = myVideoTarget; // A shared_ptr to your VideoTarget subclass.
gameshare::Host host(config);
```

`myVideoTarget` is application-provided; it must implement the rendering and retirement contract above. The native adapter avoids requiring applications to implement the VI2 display/layer plumbing themselves.

[`CpuImageStreamer`](../include/gameshare/cpu_image_streamer.hpp) is a supplied target for fixed 1280×720 RGBA8 source pixels. It requires the supported, already initialized libounce CPU-console context and keeps that context borrowed until safe cleanup. It is not a Vulkan/EGL importer. The [rendering guide](RENDERING.md) specifies the source span, synchronous copy, pinned private ABI and dependency-lifetime requirements.

[`NativeGuestDisplay`](../include/gameshare/guest_display.hpp) supplies the VI2 layer and `RendererHooks`. Its `withLayer()` operation serializes rendering against retirement and closure. The hook named `RendererHooks::close` retires renderer resources during target `retire()`; it does not close the VI2 layer. The optional `RendererHooks::owner` keeps shared renderer context alive, including after a retained failure; a raw `user` pointer alone remains caller-owned. The application supplies the renderer and its image/backing lifetime; SDK ownership does not turn a queued GPU operation into completed retirement. See [separate image rendering](RENDERING.md) for callback signatures and ownership. Do not cast a legacy default-window framebuffer into a guest layer.

The earlier Probe renderer demonstrated real separate guest images using owned RGBA8 backing and bounded presentation/retirement. That qualifies the native route; it does not automatically qualify every renderer plugged into the extracted adapter. A renderer must retire submitted images before releasing backing memory and must preserve resources if retirement cannot be proven.

This API provides an application-rendered image path. It does not accept compressed H.264/AV1 packets or supply a decoder. Decode media in the application and render its output through the target. No generic FFmpeg `AVFrame` or Vulkan-image import contract is implied by the target interface.

## Audio

With audio sharing enabled, the host uses the application's borrowed SelfController to enable its native mix-sharing flag. It reads current device volume/auto-tune values and forwards those values to StreamPlay; it does not change system volume settings. Coordinate this application-scoped audio flag with any other component that changes it. The SDK disables the flag it enabled during normal shutdown.

The flag/configuration calls are qualified; audible guest output and Moonlight's concurrent playback still need a device fixture. An enabled flag alone is not an audio test.

### Direct PCM for a separate guest

Set `HostConfig::guestAudio` and optionally `guestAudioUser` when using
`SeparateDisplay`. Set `mirrorAudio=false`. The callback runs on the native
worker and fills every requested interleaved stereo PCM16 frame at 48 kHz.
Each request asks for at most `GuestAudioMaxFrames` (512). Fill silence
when the source has nothing to play. Keep the source alive through
`stopAndWait()`. The callback must return promptly and must not call host
lifecycle methods. The SDK owns the bounded queue submission, including
partial writes. Audio errors are sent to the log callback while video and
input continue. See the [direct audio evidence](GUEST_AUDIO.md).

## Controller snapshots

`controller()` returns a copied, thread-safe `ControllerState`:

| Field | Representation |
| --- | --- |
| `connected` | A currently usable, fresh guest controller sample is available. |
| `buttons` | Raw native Npad button bits in a `std::uint64_t`. |
| `leftX`, `leftY`, `rightX`, `rightY` | Raw signed native stick axes in `std::int32_t`; positive Y points upwards. |

Snapshots are neutral when input is disabled, the host is stopping, the guest has departed, or the newest sample is older than 500 ms. `connected` describes usable controller input; it is not a peer count or an independent network-connection query. `Hosting` can precede the first usable input sample.

Apply dead zones, axis scaling, button remapping and application-specific input policy outside the SDK. Native button bits are not SDL, XInput, Sunshine or Moonlight protocol flags.

For a PC-streaming client, allocate a separate PC controller slot for the guest. Preserve local controller slots, announce the new topology, and send neutral/release state on disconnect, local slot reassignment, focus loss, stop, and PC connection loss. The guest must not enter the local unified UI controller: otherwise it could trigger overlay, keyboard, mouse or exit shortcuts. Moonlight's mapping adapter remains application code rather than part of this library.

## UI and connection transitions

Call `start()` after dismissing an in-game overlay. It starts asynchronous preparation and may open system user-selection or matchmaking UI. Continue the normal application loop; do not wait for `Hosting` in a busy loop on the UI thread.

Use `state()` for start/stop status and `takeError()` from one UI consumer. Queue a returned error until the owning view is focused and stable. Opening a modal while another activity is fading can corrupt the application's intended navigation sequence.

Use `requestStop()` for a responsive Stop action or a network-disconnect callback. It requests cancellation and immediately makes controller snapshots neutral. Before destroying the streaming session or application platform, call `stopAndWait()` while applet, display and audio services still exist, then check `canUnload()`. Only a true result permits ordinary teardown/NRO return. A false result requires preserving resources and closing the full hosting title/process through HOME. Mark an intentional PC-stream stop before joining the GameShare worker so a connection callback cannot start an unwanted reconnect during teardown.

The [lifecycle guide](LIFECYCLE.md) defines failure recovery and why an application restart can be required.
