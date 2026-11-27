#include <switch.h>
#include <switch/applets/libapplet.h>
#include <switch/applets/psel.h>
#include <gameshare/host.hpp>
#include "guest_input.hpp"
#include <sanitizer/asan_interface.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using gameshare::Host;

namespace {
using namespace std::chrono_literals;
enum class Kind { Sm, Account, User, Root, Scoped, AudioRoot, AudioFallback, AudioManager, AudioDevice, PctlRoot, Pctl, Applet, Self };
constexpr Result Failure = 0x12345;
constexpr std::array<u64, 4> Sizes {4096, 8192, 12288, 16384};
constexpr AccountUid User {{0x11111111, 0x22222222}};
struct Model {
    std::mutex mutex;
    std::map<Handle, Kind> services;
    std::map<Handle, std::string> handles;
    std::map<unsigned, std::vector<u8>> storages;
    std::vector<std::string> events;
    Handle next = 100;
    void* work = nullptr;
    size_t transferCount = 0;
    bool initialized = false, finalized = false;
    bool appletCreated = false, appletStarted = false;
    unsigned failScoped = 0;
    bool cancelResponse = false, waitInApplet = false, audioFallback = false;
    bool multipleUsers = false, cancelSelector = false, waitInSelector = false;
    bool profileLocked = false, hasLaunchProfile = false, badLaunchProfile = false;
    unsigned launchPops = 0;
    AccountUid loaderUser{};
    bool expectInput = true, expectMirrorAudio = true;
    u8 videoMode = 0;
    unsigned audioWrites = 0;
    std::atomic<unsigned> audioPulls{0};
    std::atomic<bool> appletExited {false}, mapping {false}, inputReady {true};
    std::atomic<unsigned> connectionMask {1}, inputSamples {0}, connectionCalls {0};
    std::atomic<bool> blockConnection {false}, connectionBlocked {false};
    std::mutex blockMutex;
    std::condition_variable blockCondition;
} model;

Handle newService(Kind kind) {
    const Handle handle = model.next++;
    assert(model.services.emplace(handle, kind).second);
    return handle;
}
Handle newHandle(const std::string& name) {
    const Handle handle = model.next++;
    assert(model.handles.emplace(handle, name).second);
    return handle;
}
void event(const std::string& name) { model.events.push_back(name); }
unsigned count(const std::string& name) {
    std::lock_guard<std::mutex> lock(model.mutex);
    return std::count(model.events.begin(), model.events.end(), name);
}
size_t position(const std::string& name) {
    const auto it = std::find(model.events.begin(), model.events.end(), name);
    assert(it != model.events.end());
    return it - model.events.begin();
}
template<class T> void output(void* destination, size_t size, const T& value) {
    assert(size == sizeof(value));
    std::memcpy(destination, &value, size);
}
template<class T> T input(const void* source, size_t size) {
    assert(size == sizeof(T));
    T value;
    std::memcpy(&value, source, size);
    return value;
}
template<class F> void waitFor(F predicate, const char* description, std::chrono::milliseconds timeout = 2000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error(description);
        std::this_thread::sleep_for(2ms);
    }
}
void balanced() {
    std::lock_guard<std::mutex> lock(model.mutex);
    assert(model.services.empty() && model.handles.empty() && model.storages.empty());
    assert(!model.mapping && !model.appletCreated);
    if (model.work) assert(__asan_address_is_poisoned(model.work));
    if (model.initialized) {
        if (model.expectInput) assert(position("guest:unmap") < position("scoped:3"));
        else assert(std::find(model.events.begin(), model.events.end(), "guest:map") == model.events.end());
        assert(position("scoped:3") < position("handle-close:tm"));
        if (model.videoMode == 1) {
            assert(position("target:open") < position("scoped:2"));
            assert(position("guest:unmap") < position("target:retire"));
            assert(position("target:retire") < position("pctl:1022"));
            assert(position("pctl:1022") < position("scoped:3"));
            assert(position("scoped:3") < position("target:close"));
            assert(position("target:close") < position("handle-close:tm"));
        }
    }
}
void nextSession() {
    std::lock_guard<std::mutex> lock(model.mutex);
    assert(model.services.empty() && model.handles.empty() && model.storages.empty() && !model.mapping);
    model.events.clear();
    model.work = nullptr;
    model.transferCount = 0;
    model.initialized = model.finalized = model.appletStarted = false;
    model.inputSamples = model.connectionCalls = 0;
}
void waitHosting(Host& backend) {
    waitFor([&] { return backend.state() == Host::State::Hosting; }, "native backend did not host");
    assert(!backend.canUnload());
}
void waitInput(Host& backend) {
    waitFor([&] { return backend.controller().connected; }, "native guest input was not published");
    const auto state = backend.controller();
    assert(state.buttons == 0x1234 && state.leftX == 123 && state.leftY == -456
        && state.rightX == 789 && state.rightY == -987);
}
void finish(Host& backend) {
    assert(!backend.canUnload());
    backend.requestStop();
    assert(!backend.controller().connected); // stop clears the public snapshot immediately
    backend.stopAndWait();
    assert(backend.state() == Host::State::Idle);
    assert(backend.takeError().empty());
    assert(backend.canUnload());
    balanced();
}
class TestVideoTarget final : public gameshare::VideoTarget {
public:
    Result openResult = 0, retireResult = 0, closeResult = 0;
    bool throwOpen = false, throwRetire = false, throwClose = false;
    bool ownsResources = false, rendererRetired = false;
    const std::thread::id caller = std::this_thread::get_id();
    Result open() override {
        std::lock_guard<std::mutex> lock(model.mutex);
        assert(std::this_thread::get_id() != caller && !ownsResources);
        assert(model.videoMode == 1 && !model.initialized);
        event("target:open");
        ownsResources = true; // Even a failed open can have partial resources.
        if (throwOpen) throw std::runtime_error("synthetic target open exception");
        return openResult;
    }
    Result retire() override {
        std::lock_guard<std::mutex> lock(model.mutex);
        assert(std::this_thread::get_id() != caller && ownsResources && !rendererRetired);
        assert(!model.mapping && !model.finalized);
        event("target:retire");
        if (model.initialized) {
            assert(position("guest:unmap") < position("target:retire"));
            assert(position("scoped:5") < position("target:retire"));
            if (model.expectMirrorAudio) assert(position("audio:off") < position("target:retire"));
        }
        if (throwRetire) throw std::runtime_error("synthetic target retire exception");
        if (retireResult) return retireResult;
        rendererRetired = true;
        return 0;
    }
    Result close() override {
        std::lock_guard<std::mutex> lock(model.mutex);
        assert(std::this_thread::get_id() != caller && ownsResources && rendererRetired && !model.mapping);
        event("target:close");
        if (model.initialized) {
            assert(model.finalized && position("scoped:3") < position("target:close"));
            assert(std::find(model.events.begin(), model.events.end(), "handle-close:tm") == model.events.end());
            assert(model.work && !__asan_address_is_poisoned(model.work));
        }
        if (throwClose) throw std::runtime_error("synthetic target close exception");
        if (closeResult) return closeResult;
        ownsResources = false;
        return 0;
    }
    ~TestVideoTarget() override {
        std::lock_guard<std::mutex> lock(model.mutex);
        assert(!ownsResources);
        event("target:destroy");
    }
};
struct UnblockOnExit {
    ~UnblockOnExit() {
        {
            std::lock_guard<std::mutex> lock(model.blockMutex);
            model.blockConnection = false;
        }
        model.blockCondition.notify_all();
    }
};
}

bool serviceIsActive(const Service* service) { return service->session != 0; }
void serviceCreate(Service* service, Handle handle) { service->session = handle; }
void serviceClose(Service* service) {
    std::lock_guard<std::mutex> lock(model.mutex);
    assert(service->session != 1); // borrowed self-controller must never be closed
    const auto it = model.services.find(service->session);
    assert(it != model.services.end());
    if (it->second == Kind::Scoped)
        assert(!model.initialized || model.finalized);
    model.services.erase(it);
    service->session = 0;
}
Result serviceConvertToDomain(Service* service) {
    std::lock_guard<std::mutex> lock(model.mutex);
    assert(model.services.at(service->session) == Kind::PctlRoot);
    return 0;
}
SmServiceName smEncodeName(const char* name) {
    SmServiceName result;
    std::memcpy(result.name, name, std::min(std::strlen(name), sizeof(result.name)));
    return result;
}
Result svcConnectToNamedPort(Handle* out, const char* name) {
    assert(std::string(name) == "sm:");
    std::lock_guard<std::mutex> lock(model.mutex);
    *out = newService(Kind::Sm);
    return 0;
}

Result mockServiceDispatch(Service* service, u32 command, const void* in, size_t inSize,
    void* out, size_t outSize, SfDispatchParams p) {
    std::unique_lock<std::mutex> lock(model.mutex);
    const Kind kind = service->session == 1 ? Kind::Self : model.services.at(service->session);
    auto object = [&](Kind type) {
        assert(p.out_num_objects == 1 && p.out_objects);
        p.out_objects->session = newService(type);
    };
    if (kind == Kind::Scoped) {
        event("scoped:" + std::to_string(command));
        if (model.failScoped == command)
            return Failure;
    }
    switch (kind) {
    case Kind::Sm:
        if (command == 0) { assert(p.in_send_pid && inSize == 8); return 0; }
        assert(command == 1 && p.out_handles);
        {
            const auto name = input<SmServiceName>(in, inSize);
            std::string text(name.name, name.name + sizeof(name.name));
            if (const auto zero = text.find('\0'); zero != std::string::npos) text.resize(zero);
            Kind target;
            if (text == "acc:u0") target = Kind::Account;
            else if (text == "stpl:u") target = Kind::Root;
            else if (text == "audren:u") { if (model.audioFallback) return Failure; target = Kind::AudioRoot; }
            else if (text == "aud:u") target = Kind::AudioFallback;
            else { assert(text == "pctl:a"); target = Kind::PctlRoot; }
            *p.out_handles = newService(target);
            return 0;
        }
    case Kind::Account: {
        if (command == 160) { assert(p.in_send_pid); return 0; }
        if (command == 3 || command == 2) {
            assert(p.buffers[0].size == ACC_USER_LIST_SIZE * sizeof(AccountUid));
            if (model.profileLocked) {
                assert(command == 3);
                return 0;
            }
            std::memcpy(const_cast<void*>(p.buffers[0].ptr), &User, sizeof(User));
            if (model.multipleUsers)
                std::memcpy(static_cast<u8*>(const_cast<void*>(p.buffers[0].ptr)) + sizeof(User), &User, sizeof(User));
            return 0;
        }
        if (command == 150) { output(out, outSize, u8(model.profileLocked)); return 0; }
        assert(command == 101);
        const auto selected = input<AccountUid>(in, inSize);
        assert(selected.uid[0] == User.uid[0] && selected.uid[1] == User.uid[1]);
        event("account-manager:open");
        object(Kind::User);
        return 0;
    }
    case Kind::Root:
        assert(command == 0 && input<u32>(in, inSize) == 3);
        object(Kind::Scoped);
        return 0;
    case Kind::AudioRoot:
    case Kind::AudioManager:
        assert(command == 2 && input<u64>(in, inSize) == 0x5678);
        object(Kind::AudioDevice);
        return 0;
    case Kind::AudioFallback:
        assert(command == 2 && inSize == 0);
        object(Kind::AudioManager);
        return 0;
    case Kind::AudioDevice:
        if (command == 8) { const float value = 0.75f; output(out, outSize, value); return 0; }
        assert(command == 20); output(out, outSize, u8(0)); return 0;
    case Kind::PctlRoot:
        assert(command == 0 && p.in_send_pid);
        object(Kind::Pctl);
        return 0;
    case Kind::Pctl:
        assert(command == 1 || command == 1001 || command == 1021 || command == 1022);
        event("pctl:" + std::to_string(command));
        return 0;
    case Kind::Self:
        assert(command == 2401);
        event(input<u8>(in, inSize) ? "audio:on" : "audio:off");
        return 0;
    case Kind::Applet:
        assert(command == 30);
        return 0;
    case Kind::Scoped:
        switch (command) {
        case 1: {
            const auto bytes = static_cast<const u8*>(in);
            assert(inSize == 8 && bytes[0] == 0 && bytes[1] == model.videoMode && bytes[4] == 1);
            output(out, outSize, u32(40960)); return 0;
        }
        case 9: {
            const auto bytes = static_cast<const u8*>(in);
            assert(inSize == 8 && bytes[0] == 0 && bytes[1] == model.videoMode && bytes[4] == 0);
            output(out, outSize, Sizes); return 0;
        }
        case 300: {
            const std::array<u64, 2> config {0x40000, 0};
            output(out, outSize, config);
            assert(p.out_handles && p.out_handle_attrs[0] == SfOutHandleAttr_HipcCopy);
            *p.out_handles = newHandle("hid"); return 0;
        }
        case 2:
            assert(inSize == 16 && p.in_send_pid);
            assert(static_cast<const u8*>(in)[1] == model.videoMode);
            model.initialized = true; return 0;
        case 7:
            assert(p.out_handles);
            *p.out_handles = newHandle("event"); return 0;
        case 502: output(out, outSize, u8(0)); return 0;
        case 503: output(out, outSize, model.videoMode); return 0;
        case 510: assert(inSize == 16); return 0;
        case 41:
            assert(inSize == 0 && model.videoMode == 1);
            output(out, outSize, u32(2048)); return 0;
        case 42:
            assert(input<u8>(in, inSize) == 0);
            output(out, outSize, u32(model.audioWrites < 2 ? 2048 : 1024)); return 0;
        case 40: {
            const auto words = input<std::array<u32, 2>>(in, inSize);
            assert(words[0] == 0 && words[1] == (model.audioWrites ? 384 : 512));
            assert(!p.in_send_pid && p.buffer_attrs[0] == (SfBufferAttr_In | SfBufferAttr_HipcAutoSelect));
            assert(p.buffers[0].size == words[1] * 4);
            const auto* pcm = static_cast<const std::int16_t*>(p.buffers[0].ptr);
            assert(pcm[0] == (model.audioWrites ? 256 : 0));
            output(out, outSize, u32(model.audioWrites++ ? 384 : 128)); return 0;
        }
        case 505:
            assert(input<u64>(in, inSize) == Sizes[0] && p.in_num_handles == 1);
            assert(model.handles.at(p.in_handles[0]) == "tm"); return 0;
        case 506: {
            assert(inSize == 32 && p.in_num_handles == 3);
            std::array<u64, 3> sizes;
            std::memcpy(sizes.data(), static_cast<const u8*>(in) + 8, sizeof(sizes));
            assert((sizes == std::array<u64, 3>{Sizes[1], Sizes[3], Sizes[2]}));
            for (unsigned i = 0; i < 3; ++i) assert(model.handles.at(p.in_handles[i]) == "tm");
            return 0;
        }
        case 25: case 27: assert(inSize == 4); return 0;
        case 504:
            assert(inSize == 32 && outSize == 128);
            assert(static_cast<const u8*>(in)[0] == 1 && static_cast<const u8*>(in)[1] == 1);
            assert(std::memcmp(static_cast<const u8*>(in) + 16, &User, sizeof(User)) == 0);
            std::memset(out, 0x42, outSize); return 0;
        case 4: assert(input<u32>(in, inSize) == 0); return 0;
        case 500: return 0;
        case 8: {
            model.connectionCalls.fetch_add(1);
            lock.unlock();
            if (model.blockConnection) {
                model.connectionBlocked = true;
                std::unique_lock<std::mutex> block(model.blockMutex);
                model.blockCondition.wait(block, [] { return !model.blockConnection.load(); });
            }
            output(out, outSize, std::array<u32, 2>{model.connectionMask.load(), 0});
            return 0;
        }
        case 5: return 0;
        case 3: model.finalized = true; return 0;
        default: assert(false); return Failure;
        }
    case Kind::User: assert(false); return Failure;
    }
    assert(false); return Failure;
}

Result svcCreateTransferMemory(Handle* out, void* address, u64 size, unsigned permission) {
    std::lock_guard<std::mutex> lock(model.mutex);
    assert(permission == Perm_None && model.transferCount < 4);
    constexpr std::array<u64, 4> ordered {Sizes[0], Sizes[1], Sizes[3], Sizes[2]};
    if (!model.transferCount) model.work = address;
    u64 offset = 0;
    for (size_t i = 0; i < model.transferCount; ++i) offset += ordered[i];
    assert(address == static_cast<u8*>(model.work) + offset && size == ordered[model.transferCount]);
    assert(!__asan_address_is_poisoned(address));
    ++model.transferCount;
    *out = newHandle("tm"); return 0;
}
Result svcCloseHandle(Handle handle) {
    std::lock_guard<std::mutex> lock(model.mutex);
    assert(!model.initialized || model.finalized);
    const auto it = model.handles.find(handle);
    assert(it != model.handles.end());
    event("handle-close:" + it->second);
    model.handles.erase(it); return 0;
}
void svcSleepThread(s64 ns) { std::this_thread::sleep_for(std::chrono::nanoseconds(ns)); }
u64 appletGetAppletResourceUserId() { return 0x5678; }
Service* appletGetServiceSession_SelfController() { static Service borrowed {1}; return &borrowed; }
Result appletCreateLibraryApplet(AppletHolder* holder, AppletId id, unsigned mode) {
    std::lock_guard<std::mutex> lock(model.mutex);
    assert((u32(id) == 0x01000024 || u32(id) == 0x01000010)
        && mode == LibAppletMode_AllForeground && !model.appletCreated);
    holder->s.session = newService(Kind::Applet);
    holder->id = u32(id);
    model.appletCreated = true;
    return 0;
}
AccountUid* envGetUserIdStorage() { return &model.loaderUser; }
Result appletPopLaunchParameter(AppletStorage* storage, AppletLaunchParameterKind kind) {
    assert(kind == AppletLaunchParameterKind_PreselectedUser && model.profileLocked);
    assert(++model.launchPops == 1);
    if (!model.hasLaunchProfile) return Failure;
    std::array<u8, 0x88> data{};
    constexpr u32 magic = 0xc79497ca;
    if (!model.badLaunchProfile) std::memcpy(data.data(), &magic, sizeof(magic));
    data[4] = 1;
    std::memcpy(data.data() + 8, &User, sizeof(User));
    appletCreateStorage(storage, data.size());
    return appletStorageWrite(storage, 0, data.data(), data.size());
}
Result appletCreateStorage(AppletStorage* storage, size_t size) {
    std::lock_guard<std::mutex> lock(model.mutex);
    storage->id = model.next++;
    model.storages[storage->id].resize(size); return 0;
}
Result appletStorageWrite(AppletStorage* storage, s64 offset, const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(model.mutex);
    auto& bytes = model.storages.at(storage->id);
    assert(offset >= 0 && size <= bytes.size() && size_t(offset) <= bytes.size() - size);
    std::memcpy(bytes.data() + offset, data, size); return 0;
}
Result appletStorageRead(AppletStorage* storage, s64 offset, void* data, size_t size) {
    std::lock_guard<std::mutex> lock(model.mutex);
    const auto& bytes = model.storages.at(storage->id);
    assert(offset >= 0 && size <= bytes.size() && size_t(offset) <= bytes.size() - size);
    std::memcpy(data, bytes.data() + offset, size); return 0;
}
Result appletStorageGetSize(AppletStorage* storage, s64* size) {
    std::lock_guard<std::mutex> lock(model.mutex);
    *size = model.storages.at(storage->id).size(); return 0;
}
void appletStorageClose(AppletStorage* storage) {
    std::lock_guard<std::mutex> lock(model.mutex);
    assert(model.storages.erase(storage->id) == 1);
    storage->id = 0;
}
Result appletHolderPushInData(AppletHolder* holder, AppletStorage* storage) {
    {
        std::lock_guard<std::mutex> lock(model.mutex);
        const auto& bytes = model.storages.at(storage->id);
        if (holder->id == 0x01000010) {
            assert(bytes.size() == 0xa0 && bytes[0x97] == 1);
        } else assert(bytes.size() == 128 && bytes[0] == 0x42);
    }
    appletStorageClose(storage); return 0;
}
Result appletHolderPopOutData(AppletHolder* holder, AppletStorage* storage) {
    if (holder->id == 0x01000010) {
        appletCreateStorage(storage, sizeof(PselUiReturnArg));
        const PselUiReturnArg selected {model.cancelSelector ? Failure : 0, User};
        return appletStorageWrite(storage, 0, &selected, sizeof(selected));
    }
    appletCreateStorage(storage, 8);
    const std::array<u32, 2> response {0, model.cancelResponse ? 0u : 1u};
    return appletStorageWrite(storage, 0, response.data(), sizeof(response));
}
Result appletHolderStart(AppletHolder* holder) {
    std::lock_guard<std::mutex> lock(model.mutex);
    model.appletStarted = holder->started = true;
    event(holder->id == 0x01000010 ? "selector:start" : "applet:start"); return 0;
}
bool appletHolderCheckFinished(AppletHolder* holder) {
    return !(holder->id == 0x01000010 ? model.waitInSelector : model.waitInApplet) || model.appletExited.load();
}
Result appletHolderRequestExit(AppletHolder*) { model.appletExited = true; return 0; }
void appletHolderClose(AppletHolder* holder) {
    serviceClose(&holder->s);
    std::lock_guard<std::mutex> lock(model.mutex);
    assert(model.appletCreated); model.appletCreated = false;
}
void libappletArgsCreate(LibAppletArgs* args, u32 version) { args->version = version; }
Result libappletArgsPush(LibAppletArgs* args, AppletHolder* holder) {
    assert(args->version == (holder->id == 0x01000010 ? 0x20000u : 0x10000u)); return 0;
}
Result pselShowUserSelector(AccountUid*, const PselUserSelectionSettings*) { assert(false); return Failure; }
Result pselUiCreate(PselUiSettings* settings, PselUiMode mode) {
    assert(mode == PselUiMode_UserSelector);
    *settings = {}; return 0;
}

Result guestInputOpen(Handle handle, u64 size, u64 offset) {
    std::lock_guard<std::mutex> lock(model.mutex);
    assert(model.handles.at(handle) == "hid" && size == 0x40000 && offset == 0);
    assert(!model.mapping.exchange(true)); event("guest:map"); return 0;
}
Result guestInputPoll(GuestInputSample* sample) {
    assert(model.mapping);
    *sample = {};
    if (model.inputReady) {
        sample->status = GuestInput_Ready;
        sample->sampling_number = model.inputSamples.fetch_add(1);
        sample->buttons = 0x1234;
        sample->left_x = 123; sample->left_y = -456;
        sample->right_x = 789; sample->right_y = -987;
    } else sample->status = GuestInput_NoSamples;
    return 0;
}
Result guestInputClose() {
    std::lock_guard<std::mutex> lock(model.mutex);
    if (model.mapping.exchange(false)) event("guest:unmap");
    return 0;
}
bool guestInputHasResources() { return model.mapping; }

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string scenario = argv[1];
    const bool separate = scenario.starts_with("separate_");
    const bool openFailure = scenario == "separate_open_failure" || scenario == "separate_open_throw";
    const bool closeFailure = scenario == "separate_close_failure" || scenario == "separate_close_throw";
    const bool retireFailure = scenario == "separate_retire_failure" || scenario == "separate_retire_throw";
    const bool finalizeFailure = scenario == "finalize_retained" || scenario == "separate_finalize_retained";
    const bool partialRetained = scenario == "separate_partial_retained";
    const bool retained = finalizeFailure || closeFailure || retireFailure || partialRetained;
    std::weak_ptr<gameshare::VideoTarget> provider;
    try {
      {
        gameshare::HostConfig config;
        const bool directAudio = scenario == "separate_audio" || scenario == "separate_audio_failure";
        if (scenario == "no_audio" || directAudio) config.mirrorAudio = false;
        if (directAudio) {
            config.guestAudio = [](void*, std::int16_t* pcm, u32 frames) {
                ++model.audioPulls;
                for (u32 i = 0; i < frames * 2; ++i) pcm[i] = static_cast<std::int16_t>(i);
            };
            if (scenario == "separate_audio_failure") model.failScoped = 40;
        }
        if (scenario == "no_input") config.readGuestInput = model.expectInput = false;
        if (scenario == "invalid_styles") config.supportedControllerStyles = 0;
        if (scenario == "invalid_timeout") config.matchTimeoutMs = 0;
        if (scenario == "invalid_mode") config.videoMode = static_cast<gameshare::VideoMode>(2);
        if (separate) {
            model.videoMode = 1;
            config.videoMode = gameshare::VideoMode::SeparateDisplay;
            if (scenario != "separate_missing_target") {
                auto target = std::make_shared<TestVideoTarget>();
                if (scenario == "separate_open_failure" || partialRetained) target->openResult = Failure;
                if (scenario == "separate_open_throw") target->throwOpen = true;
                if (scenario == "separate_retire_failure") target->retireResult = Failure;
                if (scenario == "separate_retire_throw") target->throwRetire = true;
                if (scenario == "separate_close_failure" || partialRetained) target->closeResult = Failure;
                if (scenario == "separate_close_throw") target->throwClose = true;
                config.videoTarget = target;
                provider = target;
            }
        }
        model.expectMirrorAudio = config.mirrorAudio;
        Host backend(config);
        assert(backend.canUnload());
        UnblockOnExit release;
        if (scenario == "cancel") model.cancelResponse = true;
        if (scenario == "prepare_failure") model.failScoped = 506;
        if (finalizeFailure) model.failScoped = 3;
        if (scenario == "admission_grace") model.connectionMask = 0;
        if (scenario == "cancel_waiting") model.waitInApplet = true;
        if (scenario == "audio_fallback") model.audioFallback = true;
        if (scenario.starts_with("selector_")) model.multipleUsers = true;
        model.profileLocked = scenario.starts_with("locked_");
        model.badLaunchProfile = scenario == "locked_bad_launch";
        model.hasLaunchProfile = scenario == "locked_launch" || model.badLaunchProfile;
        const bool launchProfileAvailable = scenario == "locked_launch" || scenario == "locked_cached_launch";
        if (scenario == "locked_cached_launch") model.loaderUser = User;
        if (scenario == "selector_cancel") model.cancelSelector = true;
        if (scenario == "selector_cancel_waiting") model.waitInSelector = true;
        backend.start();
        if (scenario == "invalid_styles" || scenario == "invalid_timeout" || scenario == "invalid_mode" || scenario == "separate_missing_target") {
            assert(backend.state() == Host::State::Failed);
            assert(backend.takeError().find("invalid host configuration") != std::string::npos);
            assert(!backend.controller().connected);
            backend.stopAndWait();
            assert(backend.canUnload());
            balanced();
            assert(count("scoped:2") == 0 && count("guest:map") == 0);
        } else if (model.profileLocked && !launchProfileAvailable) {
            waitFor([&] { return backend.state() == Host::State::Failed; }, "locked title without a profile did not fail");
            backend.stopAndWait();
            assert(backend.takeError().find("supplied no valid launch profile (0 open)") != std::string::npos);
            assert(model.launchPops == 1 && count("account-manager:open") == 0);
            assert(count("selector:start") == 0 && count("scoped:2") == 0 && backend.canUnload());
            balanced();
        } else if (openFailure || partialRetained) {
            waitFor([&] { return backend.state() == Host::State::Failed; }, "failed target open did not finish");
            assert(!backend.canUnload()); // Worker still belongs to the owner until joined.
            backend.stopAndWait();
            assert(backend.canUnload() == !partialRetained);
            const auto error = backend.takeError();
            assert(error.find("opening separate guest display") != std::string::npos);
            assert(!backend.controller().connected);
            assert(count("target:open") == 1 && count("target:retire") == 1 && count("target:close") == 1);
            assert(count("scoped:2") == 0 && count("scoped:500") == 0 && count("scoped:3") == 0);
            if (partialRetained) {
                assert(error.find("Close this software from HOME") != std::string::npos);
                std::lock_guard<std::mutex> lock(model.mutex);
                assert(!model.services.empty() && model.handles.empty() && !model.mapping);
            } else balanced();
        } else if (scenario == "cancel" || scenario == "prepare_failure" || scenario == "selector_cancel") {
            waitFor([&] { return backend.state() == (scenario == "prepare_failure" ? Host::State::Failed : Host::State::Idle); }, "cancel/failure did not finish");
            assert(!backend.canUnload());
            backend.stopAndWait();
            assert(backend.canUnload());
            assert(count("scoped:500") == 0 && count("audio:on") == 0);
            if (scenario == "prepare_failure") assert(backend.takeError().find("registering guest native buffers") != std::string::npos);
            else assert(backend.takeError().empty());
            balanced();
        } else if (scenario == "cancel_waiting" || scenario == "selector_cancel_waiting") {
            waitFor([&] { return count(scenario == "cancel_waiting" ? "applet:start" : "selector:start") == 1; }, "applet was not opened");
            finish(backend);
            assert(model.appletExited && count("scoped:500") == 0);
        } else {
            waitHosting(backend);
            if (scenario == "admission_grace") {
                waitFor([] { return model.connectionCalls >= 3; }, "initial connection polling did not run");
                assert(backend.state() == Host::State::Hosting && !backend.controller().connected);
                model.connectionMask = 1;
            }
            if (scenario == "no_input") {
                waitFor([] { return model.connectionCalls >= 3; }, "input-disabled host did not poll connection");
                assert(!backend.controller().connected && !model.mapping && model.inputSamples == 0);
                assert(count("guest:map") == 0);
            } else waitInput(backend);
            assert(count("scoped:500") == 1 && count("audio:on") == (config.mirrorAudio ? 1u : 0u));
            if (directAudio) {
                const unsigned writes = scenario == "separate_audio_failure" ? 1 : 2;
                waitFor([&] { return count("scoped:40") >= writes; }, "direct audio did not write");
                const auto polls = model.connectionCalls.load();
                waitFor([&] { return model.connectionCalls >= polls + 3; }, "audio write stopped the session");
                assert(count("scoped:41") == 1 && count("scoped:40") == writes && model.audioPulls == 1);
                assert(backend.state() == Host::State::Hosting);
            }
            if (scenario == "concurrent_hosts") {
                Host second;
                second.start();
                assert(second.state() == Host::State::Failed);
                assert(!second.canUnload());
                assert(second.takeError().find("another host is already active") != std::string::npos);
                second.stopAndWait();
                assert(count("scoped:2") == 1 && count("scoped:500") == 1 && count("guest:unmap") == 0);
                assert(model.mapping && backend.controller().connected);
                finish(backend);
                assert(second.canUnload());
                nextSession();
                second.start();
                waitHosting(second);
                waitInput(second);
                finish(second);
            } else if (scenario == "disconnect") {
                model.connectionMask = 0;
                waitFor([&] { return backend.state() == Host::State::Idle; }, "guest disconnect did not close session");
                assert(!backend.controller().connected && backend.takeError().empty());
                assert(!backend.canUnload());
                backend.stopAndWait(); assert(backend.canUnload()); balanced();
            } else if (scenario == "blocked_snapshot") {
                model.blockConnection = true;
                waitFor([] { return model.connectionBlocked.load(); }, "IPC mock did not block");
                waitFor([&] { return !backend.controller().connected; }, "blocked worker left a stale held input", 1500ms);
                assert(backend.state() == Host::State::Hosting);
                {
                    std::lock_guard<std::mutex> lock(model.blockMutex);
                    model.blockConnection = false;
                }
                model.blockCondition.notify_all();
                finish(backend);
            } else if (retained) {
                backend.stopAndWait();
                assert(backend.state() == Host::State::Failed && !backend.controller().connected);
                assert(!backend.canUnload());
                const char* failedStage = retireFailure ? "retiring separate guest display" :
                    closeFailure ? "closing separate guest display" : "finalizing native host";
                assert(backend.takeError().find(failedStage) != std::string::npos);
                {
                    std::lock_guard<std::mutex> lock(model.mutex);
                    assert(model.handles.size() == 6 && !model.services.empty());
                    assert(model.work && !__asan_address_is_poisoned(model.work));
                    assert(!model.mapping && !model.appletCreated && model.finalized == closeFailure);
                }
                const unsigned starts = count("scoped:500");
                backend.start();
                std::this_thread::sleep_for(30ms);
                assert(backend.state() == Host::State::Failed && count("scoped:500") == starts);
                assert(count("scoped:2") == 1 && count("scoped:3") == (retireFailure ? 0u : 1u) && count("handle-close:tm") == 0);
                assert(backend.takeError().find("Close this software from HOME") != std::string::npos);
                if (separate) {
                    assert(count("target:retire") == 1 && count("target:close") == (closeFailure ? 1u : 0u));
                    assert(count("pctl:1022") == (retireFailure ? 0u : 1u));
                }
            } else {
                finish(backend);
            }
            assert(count("audio:off") == (config.mirrorAudio ? 1u : 0u) && count("scoped:5") == 1);
            if (launchProfileAvailable) {
                assert(model.loaderUser.uid[0] == User.uid[0] && model.loaderUser.uid[1] == User.uid[1]);
                assert(count("account-manager:open") == 1 && count("selector:start") == 0);
                nextSession();
                Host retry(config);
                retry.start();
                waitHosting(retry);
                finish(retry);
                assert(model.launchPops == 1 && count("account-manager:open") == 1 && count("selector:start") == 0);
            }
        }
      }
        if (retained) {
            // Destruction must not free memory potentially referenced by the
            // service. Quarantine must also outlive the original instance.
            {
                std::lock_guard<std::mutex> lock(model.mutex);
                assert(model.handles.size() == (partialRetained ? 0u : 6u) && !model.services.empty());
                if (!partialRetained) assert(model.work && !__asan_address_is_poisoned(model.work));
                assert(model.finalized == closeFailure);
            }
            if (separate) {
                assert(!provider.expired() && count("target:destroy") == 0);
                const auto target = std::static_pointer_cast<TestVideoTarget>(provider.lock());
                assert(target->ownsResources && target->rendererRetired == !retireFailure);
            }
            Host replacement;
            assert(!replacement.canUnload());
            replacement.start();
            assert(replacement.state() == Host::State::Failed);
            assert(replacement.takeError().find("Close this software from HOME") != std::string::npos);
            assert(!replacement.canUnload());
            assert(count("scoped:2") == (partialRetained ? 0u : 1u) && count("scoped:500") == (partialRetained ? 0u : 1u));
        } else if (separate && scenario != "separate_missing_target") {
            assert(provider.expired() && count("target:destroy") == 1);
        }
        std::cout << "GameShare SDK actual-source lifecycle passed: " << scenario << std::endl;
    } catch (const std::exception& error) {
        std::cerr << scenario << ": " << error.what() << std::endl;
        return 1;
    }
}
