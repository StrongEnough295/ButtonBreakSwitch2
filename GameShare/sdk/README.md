# GameShare SDK for Switch 2

See [current device qualification](docs/QUALIFICATION.md). The separate-image
SDK example is working on the test consoles. Concurrent Moonlight networking
currently fails on both Wi-Fi and Ethernet during native initialization; do not
treat the Moonlight integration as a working PC-to-guest session yet.

The [native contract](docs/NATIVE_CONTRACT.md) records recovered IPC layouts,
account/permission requirements, audio and cleanup order for lower-level work.

A C++20 library for hosting one local native GameShare guest from a Switch 2 homebrew application. It uses the system's matchmaking screen and supports two video paths: mirror the application's default display, or render a separate image for the guest. It can also request application audio sharing and expose the guest's raw controller state. Mirroring remains the default used by Moonlight.

The library uses the public libounce SDK. It has no Borealis, SDL, Vulkan, FFmpeg, Moonlight, or PC gamepad dependency. The core host/controller interface uses standard C++ types; the native display adapter uses public libounce graphics types. An optional fixed RGBA8 uploader supports the pinned libounce CPU-console backend. Nintendo headers, executable code, firmware, keys, and extracted research artifacts are not distributed with it.

## Current scope

| Capability | Status |
| --- | --- |
| Local native matchmaking and one stock guest | Confirmed in the Probe research |
| Default-display mirroring | Physically confirmed by the user on 2026-11-17 |
| Separate application-rendered guest image | Compiled SDK example confirmed by the user: animated image, A/B response and normal stop/exit |
| Guest controller samples | Real button changes confirmed in Probe and in the compiled SDK example |
| Application audio sharing | Native mirroring calls succeed. Direct guest PCM is available for separate display, with audible playback pending a device test. [Evidence and API](docs/GUEST_AUDIO.md) |
| Concurrent Moonlight PC streaming and guest PC input | Integration implemented; PC networking currently fails during GameShare initialization on Wi-Fi and Ethernet |
| Internet/GameChat hosting or multiple guests | Outside this implementation |
| Encoded video/audio injection or an application-selected wire protocol | Outside this API |

The [device qualification](docs/QUALIFICATION.md) distinguishes native Probe
traces, user observations of the compiled SDK example, and the unresolved
Moonlight network-concurrency failure. Audible audio and guest input reaching
the PC remain unverified.

Mirroring shares what the application already draws. The separate-display path lets an application render a guest-specific image through a native target and renderer hooks. `CpuImageStreamer` supplies a fixed 1280×720 RGBA8 upload path for applications already using the supported CPU-console context; custom renderers use `NativeGuestDisplay`. Target cleanup retires submitted images before native finalization, then closes the layer/display after finalization. This is an image/rendering path: direct submission of compressed H.264/AV1 or encoded audio remains unrecovered. An application can decode its own media and render the resulting image; the SDK does not provide an FFmpeg decoder or a generic `AVFrame`/Vulkan-image importer. See [rendering prerequisites and ownership](docs/RENDERING.md).

## Prerequisites

- A Switch 2 application built with the public libounce Switch 2 toolchain and C++20.
- Normal initialized applet/window/audio lifecycle. Keep the application loop running while the native matchmaking screen is open.
- An existing launch context authorized for native GameShare. Research used title override under an installed GameShare-capable Switch 2 title. An arbitrary NRO launch context is not established as sufficient.
- A guest compatible with the host system's native GameShare protocol. The library does not bypass an update requirement or select an older protocol.

Use one active `gameshare::Host` per process. Stop it and check `canUnload()` before destroying display/audio services or unloading the NRO. A retained cleanup failure requires closing the entire hosting title/process from HOME; returning to hbmenu is not sufficient. See [ownership and shutdown](docs/LIFECYCLE.md).

## Add to an application

With the library directory inside your source tree:

```cmake
add_subdirectory(libraries/gameshare)
target_link_libraries(my_application PRIVATE gameshare::gameshare)
```

Configure the parent application with the libounce Switch 2 toolchain. The target links the public `nx` library supplied by libounce; this spelling does not select the Switch 1 SDK. Non-Switch-2 configuration is rejected.

`GAMESHARE_ENABLE_CPU_STREAMER` defaults to `ON` for this SDK. The optional uploader requires the qualified libounce revision `d1718bb7076f`, recorded in the devkit's `ounce-devkit-version.txt` or `ounce-sdk-version.txt`. Configure with `-DGAMESHARE_ENABLE_CPU_STREAMER=OFF` to build the host and custom-renderer interface without that uploader. Moonlight disables it by default because its renderer uses Vulkan. The exported CMake target defines `GAMESHARE_HAS_CPU_STREAMER` as `1` or `0`; applications must guard optional uploader use accordingly.

To build the static archive from this SDK directory (also the root of the
extracted ZIP), export `DEVKITPRO` pointing to the public homebrew SDK:

```sh
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/libounce/cmake/Switch2.cmake"
cmake --build build
cmake --install build --prefix /your/install/prefix
```

An installed consumer can use `find_package(gameshare CONFIG REQUIRED)` with
that prefix. The target remains `gameshare::gameshare`. To build the standalone
example, configure `examples/mirror-console` with the same toolchain and build
its default target; this produces `gameshare-mirror.nro`.

Host-only sanitizer tests (from this SDK directory):

```sh
python3 tests/native/run.py
python3 tests/guest_input/run.py
python3 tests/guest_display/run.py
python3 tests/cpu_image_streamer/run.py
```

The repository's `package.py --output <archive.zip> --library <libgameshare.a>`
creates a source/header/test/documentation bundle, optionally with our compiled
static archive. It includes portable research references and excludes dumps,
pairing keys and SDK/firmware binaries.

```cpp
#include <gameshare/host.hpp>

gameshare::HostConfig config;
gameshare::Host host(config);

// After the application lifecycle and default display are ready:
host.start();

// During the normal application loop:
const gameshare::ControllerState guest = host.controller();
const std::string error = host.takeError();

// Before renderer, audio, applet or process teardown:
host.stopAndWait();
if (host.canUnload()) {
    // Normal application/NRO teardown is now permitted.
} else {
    // Keep resources and the lifecycle loop alive; ask for a full title close.
    // Do not return to hbmenu or unload this NRO.
}
```

This is a lifecycle fragment, not a standalone application loop. Map `guest.buttons` and the stick axes into your application's input model, and send neutral/release state when `guest.connected` becomes false. Present `error` on the UI thread when it is safe to open a dialog.

- [Configuration, input and application integration](docs/INTEGRATION.md)
- [Separate guest images and renderer hooks](docs/RENDERING.md)
- [States, threads, cancellation and retained resources](docs/LIFECYCLE.md)
- [Research findings, firmware boundaries and evidence](docs/RESEARCH.md)

## License

The SDK core is [MIT licensed](LICENSE). The CPU image uploader adapted from libounce and its corresponding test support retain their per-file ISC notices. libounce and other dependencies retain their own licenses. Moonlight's application license is separate from this library's license.
