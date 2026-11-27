/* SPDX-License-Identifier: MIT */
#pragma once
#include "controller.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace gameshare {
enum class LogLevel { Info, Error };
using LogCallback = void (*)(void* user, LogLevel level, const char* message);
// Pulls interleaved stereo PCM16 at 48 kHz on the native worker. The source
// must fill every requested frame, using silence when no game is playing.
inline constexpr std::uint32_t GuestAudioMaxFrames = 512;
using GuestAudioCallback = void (*)(void* user, std::int16_t* pcm, std::uint32_t frames);
enum class VideoMode : std::uint8_t { Mirror = 0, SeparateDisplay = 1 };

// Renderer/display provider for mode1. Both calls run on the native worker.
// open prepares the display. retire stops draws and retires submitted images
// BEFORE native Finalize; close releases the layer/display AFTER Finalize.
// Both must handle partial open. Return native Result(0=success).
// A failed retirement/close retains the provider and quarantines the process.
class VideoTarget {
public:
    virtual ~VideoTarget() = default;
    virtual std::uint32_t open() = 0;
    virtual std::uint32_t retire() = 0;
    virtual std::uint32_t close() = 0;
};

struct HostConfig {
    VideoMode videoMode = VideoMode::Mirror;
    std::shared_ptr<VideoTarget> videoTarget; // Required only for SeparateDisplay.
    bool mirrorAudio = true;
    bool readGuestInput = true;
    // FullKey, Handheld and JoyDual. Other native styles are not decoded yet.
    std::uint32_t supportedControllerStyles = 7;
    // Maximum foreground matchmaking / user-selection wait,1000..600000ms.
    // Stop cancels it.
    std::uint32_t matchTimeoutMs = 240000;
    // Optional worker-thread log sink. The application owns user-data lifetime.
    LogCallback log = nullptr;
    void* logUser = nullptr;
    // Optional direct audio for SeparateDisplay. The app supplies the guest
    // source and keeps it alive through stopAndWait(). Disable mirrorAudio.
    GuestAudioCallback guestAudio = nullptr;
    void* guestAudioUser = nullptr;
};

// Switch2-only native LOCAL host, one guest, mirrored or separate guest display.
// It never connects to GameChat or changes HEN/RO, firmware, saves or account
// settings. All services/buffers are owned; applet SelfController is borrowed.
// One active Host is allowed per process. Failed cleanup quarantines the SDK
// until process exit, including newly constructed Host objects.
class Host {
public:
    enum class State { Idle, Starting, Hosting, Stopping, Failed };
    explicit Host(const HostConfig& config = {});
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // Nonblocking, serialized and idempotent while active. The application must
    // continue its normal appletMainLoop/lifecycle and display/audio rendering.
    void start();
    void requestStop();
    // Joins the worker; normal applet exit has a bounded wait. A synchronous OS
    // IPC itself cannot be timed out by this API. Call before renderer teardown.
    void stopAndWait();
    // Check after stopAndWait, before display teardown or NRO unload. False on
    // retained native references requires closing the whole title/process from
    // HOME; returning to a resident hbmenu is not sufficient recovery.
    bool canUnload() const;
    State state() const;
    // Thread-safe. Neutral when stopped, disconnected, or older than500ms.
    ControllerState controller() const;
    // Consume on the application's UI thread. No UI callback runs on the worker.
    std::string takeError();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace gameshare
