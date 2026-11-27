/* SPDX-License-Identifier: MIT */
#ifdef __SWITCH2__
#include <gameshare/host.hpp>
#include "guest_input.hpp"
#include "runtime_state.hpp"
#include <switch.h>
#include <switch/applets/libapplet.h>
#include <switch/applets/psel.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <exception>


namespace gameshare {
namespace {
constexpr u64 Page = 4096;
constexpr u64 MaxWork = 128 * 1024 * 1024;
constexpr u64 ExitTimeoutMs = 5000;
constexpr u64 InputTimeoutMs = 500;
using detail::restartRequired;
std::atomic<bool> hostInUse{false};

u64 nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
void sleepMs(u64 ms) { svcSleepThread(ms * 1000000); }
Result badInput() { return MAKERESULT(Module_Libnx, LibnxError_BadInput); }

AccountUid preselectedApplicationUser() {
    // The applet launch parameter is consumed when popped. Keep its validated
    // identity available for later GameShare sessions in this process.
    static const AccountUid selected = [] {
        struct LaunchProfile {
            u32 magic;
            u8 selected;
            u8 reserved[3];
            AccountUid uid;
        };
        static_assert(sizeof(LaunchProfile) == 0x18 && offsetof(LaunchProfile, uid) == 8);
        // The launcher can preserve the same process's launch selection
        // across NRO loads, including after another NRO read the parameter.
        AccountUid* loaderUser = envGetUserIdStorage();
        AppletStorage storage{};
        if (R_FAILED(appletPopLaunchParameter(&storage, AppletLaunchParameterKind_PreselectedUser))) {
            return loaderUser ? *loaderUser : AccountUid{};
        }
        s64 size = 0;
        LaunchProfile profile{};
        const Result rc = appletStorageGetSize(&storage, &size);
        const bool valid = R_SUCCEEDED(rc) && size >= 0x88 &&
            R_SUCCEEDED(appletStorageRead(&storage, 0, &profile, sizeof(profile))) &&
            profile.magic == 0xc79497ca && profile.selected == 1 && accountUidIsValid(&profile.uid);
        appletStorageClose(&storage);
        if (!valid) return AccountUid{};
        if (loaderUser) *loaderUser = profile.uid;
        return profile.uid;
    }();
    return selected;
}

struct MemoryConfig { u8 type, mode; u16 reserved; u32 guests; };
struct InitializeConfig { MemoryConfig config; u64 pid; };
struct GuestBuffers { u32 guest, reserved; u64 sizes[3]; };
struct MatchConfig {
    u8 minGuests, maxGuests, minPlayers, maxPlayers;
    u32 reserved, flags;
    u16 features, other;
    AccountUid user;
};
struct AudioConfig { u8 autoTune; u8 padding[3]; float speaker, tv, external; };
static_assert(sizeof(MemoryConfig) == 8 && sizeof(InitializeConfig) == 16);
static_assert(sizeof(GuestBuffers) == 32 && sizeof(MatchConfig) == 32);
static_assert(sizeof(AudioConfig) == 16);
struct GuestAudioWrite { u8 guest; u8 reserved[3]; u32 frames; };
static_assert(sizeof(GuestAudioWrite) == 8 && offsetof(GuestAudioWrite, frames) == 4);
constexpr u32 GuestAudioTargetFrames = 1024;
struct GuestAudioFeed {
    std::array<std::int16_t, GuestAudioMaxFrames * 2> pcm{};
    u32 capacity = 0, pending = 0, offset = 0;
    bool disabled = false, loggedWrite = false;
};
}

struct Host::Impl {
    explicit Impl(const HostConfig& options) : config(options) {}
    HostConfig config;
    std::atomic<State> status{State::Idle};
    std::atomic<bool> stopping{false};
    std::mutex workerMutex, snapshotMutex;
    std::thread worker;
    ControllerState sample;
    u64 sampleTime = 0;
    std::string error, failure;
    bool cancelled = false;

    Service sm{}, account{}, userManager{}, root{}, scoped{}, pctlRoot{}, pctl{};
    Handle hid = INVALID_HANDLE, connectionEvent = INVALID_HANDLE;
    Handle transfers[4]{};
    void* work = nullptr;
    u64 hidSize = 0, hidOffset = 0;
    AccountUid uid{};
    AppletHolder holder{};
    bool initialized = false, communicating = false, matched = false;
    bool holderCreated = false, holderStarted = false, mirrorAudio = false;
    bool retained = false;
    bool leaseOwned = false;
    bool targetOpened = false;
    bool targetRetired = false;

    void log(LogLevel level, const char* message) {
        // Diagnostic code must never interrupt native cleanup.
        if (config.log) {
            try { config.log(config.logUser, level, message); }
            catch (...) {}
        }
    }
    bool check(Result rc, const char* stage) {
        if (R_SUCCEEDED(rc)) return true;
        char text[256];
        std::snprintf(text, sizeof(text), "GameShare: %s failed (0x%X).", stage, unsigned(rc));
        if (failure.empty()) failure = text;
        log(LogLevel::Error, text);
        return false;
    }
    Result open(const char* name, Service* out) {
        // A new initialized SM session avoids sharing the library's singleton
        // across the UI/audio threads. Only this process's normal access is used.
        if (!serviceIsActive(&sm)) {
            Handle handle = INVALID_HANDLE;
            Result rc = svcConnectToNamedPort(&handle, "sm:");
            if (R_FAILED(rc)) return rc;
            serviceCreate(&sm, handle);
            const u64 zero = 0;
            rc = serviceDispatchIn(&sm, 0, zero, .in_send_pid = true);
            if (R_FAILED(rc)) return rc;
        }
        const SmServiceName encoded = smEncodeName(name);
        Handle handle = INVALID_HANDLE;
        Result rc = serviceDispatchIn(&sm, 1, encoded,
            .out_handle_attrs = {SfOutHandleAttr_HipcMove}, .out_handles = &handle);
        if (R_SUCCEEDED(rc)) serviceCreate(out, handle);
        return rc;
    }
    void clearSample() {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        sample = {};
        sampleTime = 0;
    }
    bool openApplet(AppletId id, u32 version, const void* input, size_t size) {
        if (stopping) return false;
        if (!check(appletCreateLibraryApplet(&holder, id, LibAppletMode_AllForeground), "opening native applet"))
            return false;
        holderCreated = true;
        LibAppletArgs args{};
        libappletArgsCreate(&args, version);
        if (!check(libappletArgsPush(&args, &holder), "sending common applet arguments")) return false;
        AppletStorage storage{};
        if (!check(appletCreateStorage(&storage, size), "creating native applet arguments")) return false;
        Result rc = appletStorageWrite(&storage, 0, input, size);
        if (R_SUCCEEDED(rc)) rc = appletHolderPushInData(&holder, &storage);
        else appletStorageClose(&storage);
        if (!check(rc, "sending native applet arguments") ||
            !check(appletHolderStart(&holder), "starting native applet")) return false;
        holderStarted = true;
        return true;
    }
    bool waitApplet() {
        const u64 deadline = nowMs() + config.matchTimeoutMs;
        while (!appletHolderCheckFinished(&holder) && !stopping && nowMs() < deadline) sleepMs(25);
        if (stopping || !appletHolderCheckFinished(&holder)) { cancelled = true; return false; }
        return check(serviceDispatch(&holder.s, 30), "completing native applet");
    }
    bool readAppletOutput(void* output, size_t size) {
        AppletStorage storage{};
        if (!check(appletHolderPopOutData(&holder, &storage), "reading native applet result")) return false;
        s64 bytes = 0;
        Result rc = appletStorageGetSize(&storage, &bytes);
        if (R_SUCCEEDED(rc)) {
            if (bytes < static_cast<s64>(size) || bytes > 65536) rc = badInput();
            else rc = appletStorageRead(&storage, 0, output, size);
        }
        appletStorageClose(&storage);
        appletHolderClose(&holder); holderCreated = holderStarted = false;
        return check(rc, "validating native applet result");
    }
    bool selectLaunchUser(unsigned openCount) {
        uid = preselectedApplicationUser();
        if (accountUidIsValid(&uid)) {
            log(LogLevel::Info, "GameShare: using the launch title's selected profile.");
            return true;
        }
        char text[240];
        std::snprintf(text, sizeof(text),
            "GameShare: the launch title locks profile switching and supplied no valid launch profile (%u open).",
            openCount);
        failure = text;
        log(LogLevel::Error, text);
        return false;
    }
    bool selectUser() {
        if (!check(open("acc:u0", &account), "opening account service")) return false;
        const u64 zero = 0;
        if (!check(serviceDispatchIn(&account, 160, zero, .in_send_pid = true), "binding account context"))
            return false;
        AccountUid users[ACC_USER_LIST_SIZE]{};
        if (!check(serviceDispatch(&account, 3,
                .buffer_attrs = {SfBufferAttr_Out | SfBufferAttr_HipcPointer},
                .buffers = {{users, sizeof(users)}}), "reading open users")) return false;
        unsigned count = 0;
        while (count < ACC_USER_LIST_SIZE && accountUidIsValid(&users[count])) ++count;
        if (count == 1) {
            uid = users[0];
        } else {
            u8 locked = 0;
            if (!check(serviceDispatchOut(&account, 150, locked), "reading account switch lock")) return false;
            if (locked) {
                if (!selectLaunchUser(count)) return false;
            } else if (!selectLocalUser()) {
                return false;
            }
        }
        if (!accountUidIsValid(&uid)) return check(badInput(), "validating selected user");
        return check(serviceDispatchIn(&account, 101, uid,
            .out_num_objects = 1, .out_objects = &userManager), "retaining selected user");
    }
    bool selectLocalUser() {
        AccountUid users[ACC_USER_LIST_SIZE]{};
        if (!check(serviceDispatch(&account, 2,
                .buffer_attrs = {SfBufferAttr_Out | SfBufferAttr_HipcPointer},
                .buffers = {{users, sizeof(users)}}), "reading local users")) return false;
        if (accountUidIsValid(&users[0]) && !accountUidIsValid(&users[1])) {
            uid = users[0];
            return true;
        }

        // Use the public player-selector ABI with our own cancellable holder.
        PselUiSettings settings{};
        if (!check(pselUiCreate(&settings, PselUiMode_UserSelector), "creating user selector")) return false;
        settings.settings.unk_x97 = 1;
        PselUiReturnArg selected{};
        if (!openApplet(static_cast<AppletId>(0x01000010), 0x20000, &settings, sizeof(settings)) ||
            !waitApplet() || !readAppletOutput(&selected, sizeof(selected))) return false;
        if (R_FAILED(selected.res) || !accountUidIsValid(&selected.user_id)) { cancelled = true; return false; }
        uid = selected.user_id;
        return true;
    }
    bool readAudio(AudioConfig& config) {
        Service audioRoot{}, manager{}, device{};
        bool separateManager = false;
        Result rc = open("audren:u", &audioRoot);
        if (R_FAILED(rc)) {
            rc = open("aud:u", &audioRoot);
            if (R_SUCCEEDED(rc)) {
                rc = serviceDispatch(&audioRoot, 2, .out_num_objects = 1, .out_objects = &manager);
                separateManager = R_SUCCEEDED(rc);
            }
        }
        Service* managerSession = separateManager ? &manager : &audioRoot;
        if (R_SUCCEEDED(rc)) {
            const u64 aruid = appletGetAppletResourceUserId();
            rc = serviceDispatchIn(managerSession, 2, aruid, .out_num_objects = 1, .out_objects = &device);
        }
        const char* names[] = {"AudioBuiltInSpeakerOutput", "AudioTvOutput", "AudioExternalOutput"};
        float volumes[3]{};
        for (unsigned i = 0; R_SUCCEEDED(rc) && i < 3; ++i) {
            AudioDeviceName name{};
            std::snprintf(name.name, sizeof(name.name), "%s", names[i]);
            rc = serviceDispatchOut(&device, 8, volumes[i],
                .buffer_attrs = {SfBufferAttr_In | SfBufferAttr_HipcAutoSelect},
                .buffers = {{&name, sizeof(name)}});
            if (R_SUCCEEDED(rc) && (!std::isfinite(volumes[i]) || volumes[i] < 0 || volumes[i] > 1))
                rc = badInput();
        }
        if (R_SUCCEEDED(rc)) rc = serviceDispatchOut(&device, 20, config.autoTune);
        if (R_SUCCEEDED(rc) && config.autoTune > 1) rc = badInput();
        if (serviceIsActive(&device)) serviceClose(&device);
        if (serviceIsActive(&manager)) serviceClose(&manager);
        if (serviceIsActive(&audioRoot)) serviceClose(&audioRoot);
        config.speaker = volumes[0]; config.tv = volumes[1]; config.external = volumes[2];
        return check(rc, "reading current audio output");
    }
    bool updateAudio() {
        AudioConfig audio{};
        return readAudio(audio) && check(serviceDispatchIn(&scoped, 510, audio), "mirroring audio output settings");
    }
    bool prepare() {
        if (!selectUser() || stopping) return false;
        if (!check(open("stpl:u", &root), "opening native StreamPlay")) return false;
        const u32 factoryVersion = 3;
        if (!check(serviceDispatchIn(&root, 0, factoryVersion,
                .out_num_objects = 1, .out_objects = &scoped), "creating native host")) return false;

        // Local type0, one guest. Mode0 mirrors Default; mode1 uses the caller's
        // separately rendered player1 display, prepared before Initialize.
        const MemoryConfig memoryConfig{0, static_cast<u8>(config.videoMode), 0, 1};
        u32 required = 0;
        if (!check(serviceDispatchInOut(&scoped, 1, memoryConfig, required), "querying native work memory")) return false;
        MemoryConfig query = memoryConfig; query.guests = 0;
        u64 sizes[4]{};
        if (!check(serviceDispatchInOut(&scoped, 9, query, sizes), "querying native buffer layout")) return false;
        u64 total = 0;
        for (u64 size : sizes) {
            if (size > MaxWork || size % Page || total > MaxWork - size)
                return check(badInput(), "validating native buffer layout");
            total += size;
        }
        if (!total || total != required) return check(badInput(), "validating native work size");
        work = std::aligned_alloc(Page, total);
        if (!work) return check(MAKERESULT(Module_Libnx, LibnxError_OutOfMemory), "allocating native work memory");
        std::memset(work, 0, total);
        if (config.videoMode == VideoMode::SeparateDisplay) {
            // Even a failed open may own partial display/renderer resources.
            targetOpened = true;
            Result rc = badInput();
            try { rc = config.videoTarget->open(); } catch (...) {}
            if (!check(rc, "opening separate guest display")) return false;
        }
        u64 hidConfig[2]{};
        if (!check(serviceDispatchOut(&scoped, 300, hidConfig,
                .out_handle_attrs = {SfOutHandleAttr_HipcCopy}, .out_handles = &hid), "opening guest controller memory"))
            return false;
        hidSize = hidConfig[0]; hidOffset = hidConfig[1];
        if (config.readGuestInput && !check(guestInputOpen(hid, hidSize, hidOffset), "mapping guest controller memory")) return false;
        const InitializeConfig initialize{memoryConfig, 0};
        if (!check(serviceDispatchIn(&scoped, 2, initialize, .in_send_pid = true), "initializing native host")) return false;
        initialized = true;
        if (!check(serviceDispatch(&scoped, 7,
                .out_handle_attrs = {SfOutHandleAttr_HipcCopy}, .out_handles = &connectionEvent), "opening connection event"))
            return false;
        u8 actualType = 255, actualMode = 255;
        if (!check(serviceDispatchOut(&scoped, 502, actualType), "checking local host type") ||
            !check(serviceDispatchOut(&scoped, 503, actualMode), "checking mirroring mode")) return false;
        if (actualType != memoryConfig.type || actualMode != memoryConfig.mode) return check(badInput(), "validating mirroring mode");
        if (!updateAudio()) return false;

        Result rc = badInput();
        for (const char* name : {"pctl:a", "pctl:s", "pctl:r", "pctl"}) {
            rc = open(name, &pctlRoot);
            if (R_SUCCEEDED(rc)) break;
        }
        if (!check(rc, "opening parental control service") ||
            !check(serviceConvertToDomain(&pctlRoot), "opening communication permission context")) return false;
        const u64 zero = 0;
        if (!check(serviceDispatchIn(&pctlRoot, 0, zero, .in_send_pid = true,
                .out_num_objects = 1, .out_objects = &pctl), "creating communication permission context") ||
            !check(serviceDispatch(&pctl, 1), "initializing communication permission context") ||
            !check(serviceDispatch(&pctl, 1001), "checking communication permission") ||
            !check(serviceDispatch(&pctl, 1021), "beginning permitted communication")) return false;
        communicating = true;

        const u64 ordered[] = {sizes[0], sizes[1], sizes[3], sizes[2]};
        u64 offset = 0;
        for (unsigned i = 0; i < 4; ++i) {
            if (ordered[i] && !check(svcCreateTransferMemory(&transfers[i],
                    static_cast<u8*>(work) + offset, ordered[i], Perm_None), "creating native transfer memory"))
                return false;
            offset += ordered[i];
        }
        if (!check(serviceDispatchIn(&scoped, 505, ordered[0],
                .in_num_handles = 1, .in_handles = {transfers[0]}), "registering global native buffer")) return false;
        const GuestBuffers guest{0, 0, {ordered[1], ordered[2], ordered[3]}};
        if (!check(serviceDispatchIn(&scoped, 506, guest, .in_num_handles = 3,
                .in_handles = {transfers[1], transfers[2], transfers[3]}), "registering guest native buffers")) return false;
        const u32 styles = config.supportedControllerStyles;
        const u32 hold = 1;
        if (!check(serviceDispatchIn(&scoped, 25, styles), "configuring guest controllers") ||
            !check(serviceDispatchIn(&scoped, 27, hold), "configuring guest controller orientation")) return false;
        return true;
    }
    bool match() {
        const MatchConfig config{1, 1, 1, 1, 0, 0, 3, 0, uid};
        u8 arguments[128]{};
        if (stopping || !check(serviceDispatchInOut(&scoped, 504, config, arguments), "preparing local matchmaking"))
            return false;
        u32 response[2]{};
        if (!openApplet(static_cast<AppletId>(0x01000024), 0x10000, arguments, sizeof(arguments)) ||
            !waitApplet() || !readAppletOutput(response, sizeof(response))) return false;
        if (response[1] == 0) { cancelled = true; return false; }
        if (response[1] != 1) return check(badInput(), "validating guest count");
        matched = true;
        return true;
    }
    bool startMirroring() {
        if (stopping) return false;
        const u8 enabled = 1;
        // Borrowed ISelfController, command2401, one inline bool; no ownership
        // or global audio setting changes. This enables the application's mix.
        if (config.mirrorAudio && !check(serviceDispatchIn(appletGetServiceSession_SelfController(), 2401, enabled),
                "enabling mirrored audio")) return false;
        mirrorAudio = config.mirrorAudio;
        const u32 guest = 0;
        return check(serviceDispatchIn(&scoped, 4, guest), "enabling guest streaming") &&
               check(serviceDispatch(&scoped, 500), "starting native streaming");
    }
    void disableGuestAudio(GuestAudioFeed& feed, const char* stage, Result rc) {
        feed.disabled = true;
        char text[128];
        std::snprintf(text, sizeof(text), "GameShare: guest audio %s failed (0x%X).", stage, unsigned(rc));
        log(LogLevel::Error, text);
    }
    void pumpGuestAudio(GuestAudioFeed& feed) {
        if (!config.guestAudio || feed.disabled) return;
        if (!feed.capacity) {
            const Result rc = serviceDispatchOut(&scoped, 41, feed.capacity);
            if (R_FAILED(rc) || !feed.capacity || feed.capacity > 48000) {
                disableGuestAudio(feed, "capacity", R_FAILED(rc) ? rc : badInput());
                return;
            }
            char message[96];
            std::snprintf(message, sizeof(message), "GameShare: guest audio capacity %u frames.", unsigned(feed.capacity));
            log(LogLevel::Info, message);
        }
        constexpr u8 guest = 0;
        u32 free = 0;
        Result rc = serviceDispatchInOut(&scoped, 42, guest, free);
        if (R_FAILED(rc) || free > feed.capacity) {
            disableGuestAudio(feed, "free space", R_FAILED(rc) ? rc : badInput());
            return;
        }
        const u32 target = std::min(feed.capacity, GuestAudioTargetFrames);
        const u32 filled = feed.capacity - free;
        if (filled >= target) return;
        u32 frames = std::min(target - filled, GuestAudioMaxFrames);
        if (feed.pending) {
            frames = std::min(frames, feed.pending);
        } else {
            try { config.guestAudio(config.guestAudioUser, feed.pcm.data(), frames); }
            catch (...) { disableGuestAudio(feed, "callback", badInput()); return; }
            feed.pending = frames;
            feed.offset = 0;
        }
        const GuestAudioWrite request{guest, {}, frames};
        u32 accepted = 0;
        rc = serviceDispatchInOut(&scoped, 40, request, accepted,
            .buffer_attrs = {SfBufferAttr_In | SfBufferAttr_HipcAutoSelect},
            .buffers = {{feed.pcm.data() + feed.offset * 2, frames * 2 * sizeof(std::int16_t)}});
        if (R_FAILED(rc) || accepted > frames) {
            disableGuestAudio(feed, "submit PCM", R_FAILED(rc) ? rc : badInput());
            return;
        }
        feed.offset += accepted;
        feed.pending -= accepted;
        if (accepted && !feed.loggedWrite) {
            feed.loggedWrite = true;
            char message[96];
            std::snprintf(message, sizeof(message), "GameShare: guest audio accepted %u PCM frames.", unsigned(accepted));
            log(LogLevel::Info, message);
        }
    }
    void poll() {
        status = State::Hosting;
        log(LogLevel::Info, config.videoMode == VideoMode::Mirror
            ? "GameShare: native local mirroring started" : "GameShare: native separate display started");
        u64 lastSample = 0, lastSampleNumber = UINT64_MAX, nextAudio = nowMs() + 1000;
        const u64 connectionDeadline = nowMs() + 2000;
        bool guestSeen = false;
        GuestAudioFeed guestAudioFeed;
        while (!stopping) {
            u32 connection[2]{};
            if (!check(serviceDispatchOut(&scoped, 8, connection), "checking guest connection")) return;
            if (connection[1]) { check(connection[1], "native guest connection"); return; }
            if (!(connection[0] & 1)) {
                clearSample();
                // StartStreaming can return before the first connected event.
                if (guestSeen || nowMs() >= connectionDeadline) return;
                sleepMs(8);
                continue;
            }
            guestSeen = true;
            const u64 now = nowMs();
            if (config.readGuestInput) {
                GuestInputSample input{};
                if (!check(guestInputPoll(&input), "reading guest controller")) return;
                if (input.status == GuestInput_Ready && input.sampling_number != lastSampleNumber) {
                    lastSample = now; lastSampleNumber = input.sampling_number;
                    std::lock_guard<std::mutex> lock(snapshotMutex);
                    sample = {true, input.buttons, input.left_x, input.left_y, input.right_x, input.right_y};
                    sampleTime = now;
                } else if (now - lastSample > InputTimeoutMs) {
                    clearSample(); // Never leave a held button after stalled HID.
                }
            }
            pumpGuestAudio(guestAudioFeed);
            if (now >= nextAudio) {
                if (!updateAudio()) return;
                nextAudio = now + 1000;
            }
            sleepMs(8);
        }
    }
    bool cleanup() {
        clearSample();
        if (holderCreated) {
            if (holderStarted && !appletHolderCheckFinished(&holder)) {
                if (!check(appletHolderRequestExit(&holder), "requesting matchmaking exit")) return false;
                const u64 deadline = nowMs() + ExitTimeoutMs;
                while (!appletHolderCheckFinished(&holder) && nowMs() < deadline) sleepMs(25);
                if (!appletHolderCheckFinished(&holder)) return check(badInput(), "waiting for matchmaking to close");
            }
            appletHolderClose(&holder); holderCreated = holderStarted = false;
        }
        if (matched) {
            if (!check(serviceDispatch(&scoped, 5), "closing native session")) return false;
            matched = false;
        }
        if (mirrorAudio) {
            const u8 disabled = 0;
            if (!check(serviceDispatchIn(appletGetServiceSession_SelfController(), 2401, disabled),
                    "disabling mirrored audio")) return false;
            mirrorAudio = false;
        }
        if (!check(guestInputClose(), "unmapping guest controller")) return false;
        if (targetOpened) {
            Result rc = badInput();
            try { rc = config.videoTarget->retire(); } catch (...) {}
            if (!check(rc, "retiring separate guest display")) return false;
            targetRetired = true;
        }
        if (communicating) {
            if (!check(serviceDispatch(&pctl, 1022), "ending communication context")) return false;
            communicating = false;
        }
        if (initialized) {
            if (!check(serviceDispatch(&scoped, 3), "finalizing native host")) return false;
            initialized = false;
        }
        if (targetOpened && targetRetired) {
            Result rc = badInput();
            try { rc = config.videoTarget->close(); } catch (...) {}
            if (!check(rc, "closing separate guest display")) return false;
            targetOpened = targetRetired = false;
        }
        for (Handle* handle : {&hid, &connectionEvent, &transfers[0], &transfers[1], &transfers[2], &transfers[3]}) {
            if (*handle) {
                if (!check(svcCloseHandle(*handle), "closing owned native memory handle")) return false;
                *handle = INVALID_HANDLE;
            }
        }
        std::free(work); work = nullptr;
        for (Service* service : {&pctl, &pctlRoot, &scoped, &root, &userManager, &account, &sm})
            if (serviceIsActive(service)) serviceClose(service);
        std::memset(&uid, 0, sizeof(uid));
        return true;
    }
    void run() {
        failure.clear(); cancelled = false;
        if (prepare() && !stopping && match() && !stopping && startMirroring()) poll();
        status = State::Stopping;
        retained = !cleanup();
        if (retained) {
            restartRequired = true;
            // Keep every referenced allocation and handle alive if finalization
            // fails. Do not retry hosting or free memory still held by a service.
            failure += " Close this software from HOME. Do not return to HBMenu while native resources remain.";
        } else if (leaseOwned) {
            leaseOwned = false;
            hostInUse = false;
        }
        {
            std::lock_guard<std::mutex> lock(snapshotMutex);
            error = failure;
        }
        status = failure.empty() ? State::Idle : State::Failed;
        log(LogLevel::Info, retained ? "GameShare: native cleanup requires app restart" : "GameShare: native cleanup complete");
    }
};

Host::Host(const HostConfig& config) : impl(std::make_unique<Impl>(config)) {}
Host::~Host() {
    stopAndWait();
    // Retain the complete owner, including video-provider references/vtables.
    // The caller must honor canUnload and close the hosting title normally.
    if (impl->retained) (void)impl.release();
}
void Host::start() {
    std::lock_guard<std::mutex> lock(impl->workerMutex);
    if (impl->status == State::Starting || impl->status == State::Hosting || impl->status == State::Stopping)
        return;
    if (impl->worker.joinable()) impl->worker.join();
    if (impl->retained || restartRequired) {
        std::lock_guard<std::mutex> snapshot(impl->snapshotMutex);
        impl->error = "Close this software from HOME before trying GameShare again; do not return to HBMenu.";
        impl->status = State::Failed;
        return;
    }
    if ((impl->config.videoMode != VideoMode::Mirror && impl->config.videoMode != VideoMode::SeparateDisplay) ||
        (impl->config.videoMode == VideoMode::SeparateDisplay && !impl->config.videoTarget) ||
        (impl->config.guestAudio && (impl->config.videoMode != VideoMode::SeparateDisplay || impl->config.mirrorAudio)) ||
        !impl->config.supportedControllerStyles || (impl->config.supportedControllerStyles & ~7u) ||
        impl->config.matchTimeoutMs < 1000 || impl->config.matchTimeoutMs > 600000) {
        std::lock_guard<std::mutex> snapshot(impl->snapshotMutex);
        impl->error = "GameShare: invalid host configuration.";
        impl->status = State::Failed;
        return;
    }
    bool expected = false;
    if (!hostInUse.compare_exchange_strong(expected, true)) {
        std::lock_guard<std::mutex> snapshot(impl->snapshotMutex);
        impl->error = "GameShare: another host is already active in this application.";
        impl->status = State::Failed;
        return;
    }
    impl->leaseOwned = true;
    { std::lock_guard<std::mutex> snapshot(impl->snapshotMutex); impl->error.clear(); impl->sample = {}; }
    impl->stopping = false;
    impl->status = State::Starting;
    try {
        impl->worker = std::thread([this] { impl->run(); });
    } catch (const std::exception&) {
        impl->leaseOwned = false;
        hostInUse = false;
        std::lock_guard<std::mutex> snapshot(impl->snapshotMutex);
        impl->error = "GameShare: could not create the native worker.";
        impl->status = State::Failed;
    }
}
void Host::requestStop() {
    impl->stopping = true;
    impl->clearSample();
}
void Host::stopAndWait() {
    std::lock_guard<std::mutex> lock(impl->workerMutex);
    requestStop();
    if (impl->worker.joinable()) impl->worker.join();
}
Host::State Host::state() const { return impl->status.load(); }
bool Host::canUnload() const {
    std::lock_guard<std::mutex> lock(impl->workerMutex);
    return !impl->worker.joinable() && !impl->retained && !restartRequired && !hostInUse;
}
ControllerState Host::controller() const {
    std::lock_guard<std::mutex> lock(impl->snapshotMutex);
    if (impl->stopping || impl->status != State::Hosting || !impl->sampleTime ||
        nowMs() - impl->sampleTime > InputTimeoutMs) return {};
    return impl->sample;
}
std::string Host::takeError() {
    std::lock_guard<std::mutex> lock(impl->snapshotMutex);
    std::string result;
    result.swap(impl->error);
    return result;
}
} // namespace gameshare
#endif
