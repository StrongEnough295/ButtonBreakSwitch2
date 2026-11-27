#pragma once

#include <cstddef>
#include <cstdint>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s32 = int32_t;
using s64 = int64_t;
using Result = uint32_t;
using Handle = uint32_t;

constexpr Handle INVALID_HANDLE = 0;
constexpr unsigned Module_Libnx = 345;
constexpr unsigned LibnxError_BadInput = 1;
constexpr unsigned LibnxError_OutOfMemory = 2;
#define MAKERESULT(module, description) (Result((module) | ((description) << 9)))
#define R_SUCCEEDED(result) ((result) == 0)
#define R_FAILED(result) ((result) != 0)

struct Service { Handle session = 0; };
struct AccountUid { u64 uid[2] = {}; };
constexpr unsigned ACC_USER_LIST_SIZE = 8;
inline bool accountUidIsValid(const AccountUid* uid) { return uid->uid[0] || uid->uid[1]; }
struct SmServiceName { char name[8] = {}; };
struct AudioDeviceName { char name[256] = {}; };
struct AppletHolder { Service s; bool started = false; u32 id = 0; };
struct AppletStorage { unsigned id = 0; };
enum AppletLaunchParameterKind : u32 { AppletLaunchParameterKind_PreselectedUser = 2 };
enum AppletId : uint32_t {};
constexpr unsigned LibAppletMode_AllForeground = 0;
constexpr u32 HidNpadStyleSet_NpadFullCtrl = 7;
constexpr unsigned Perm_None = 0;
constexpr unsigned SfOutHandleAttr_HipcMove = 1;
constexpr unsigned SfOutHandleAttr_HipcCopy = 2;
constexpr unsigned SfBufferAttr_Out = 1;
constexpr unsigned SfBufferAttr_In = 2;
constexpr unsigned SfBufferAttr_HipcPointer = 4;
constexpr unsigned SfBufferAttr_HipcAutoSelect = 8;

struct SfBuffer { const void* ptr = nullptr; size_t size = 0; };
struct SfDispatchParams {
    u32 buffer_attrs[8] = {};
    SfBuffer buffers[8] = {};
    bool in_send_pid = false;
    unsigned in_num_handles = 0;
    Handle in_handles[8] = {};
    unsigned out_num_objects = 0;
    Service* out_objects = nullptr;
    unsigned out_handle_attrs[8] = {};
    Handle* out_handles = nullptr;
};

Result mockServiceDispatch(Service*, u32, const void*, size_t, void*, size_t, SfDispatchParams);
#define serviceDispatch(service, id, ...) \
    mockServiceDispatch((service), (id), nullptr, 0, nullptr, 0, SfDispatchParams{__VA_ARGS__})
#define serviceDispatchIn(service, id, input, ...) \
    mockServiceDispatch((service), (id), &(input), sizeof(input), nullptr, 0, SfDispatchParams{__VA_ARGS__})
#define serviceDispatchOut(service, id, output, ...) \
    mockServiceDispatch((service), (id), nullptr, 0, &(output), sizeof(output), SfDispatchParams{__VA_ARGS__})
#define serviceDispatchInOut(service, id, input, output, ...) \
    mockServiceDispatch((service), (id), &(input), sizeof(input), &(output), sizeof(output), SfDispatchParams{__VA_ARGS__})

bool serviceIsActive(const Service*);
void serviceCreate(Service*, Handle);
void serviceClose(Service*);
Result serviceConvertToDomain(Service*);
SmServiceName smEncodeName(const char*);
Result svcConnectToNamedPort(Handle*, const char*);
Result svcCreateTransferMemory(Handle*, void*, u64, unsigned);
Result svcCloseHandle(Handle);
void svcSleepThread(s64);
u64 appletGetAppletResourceUserId();
Service* appletGetServiceSession_SelfController();
Result appletCreateLibraryApplet(AppletHolder*, AppletId, unsigned);
AccountUid* envGetUserIdStorage();
Result appletPopLaunchParameter(AppletStorage*, AppletLaunchParameterKind);
Result appletCreateStorage(AppletStorage*, size_t);
Result appletStorageWrite(AppletStorage*, s64, const void*, size_t);
Result appletStorageRead(AppletStorage*, s64, void*, size_t);
Result appletStorageGetSize(AppletStorage*, s64*);
void appletStorageClose(AppletStorage*);
Result appletHolderPushInData(AppletHolder*, AppletStorage*);
Result appletHolderPopOutData(AppletHolder*, AppletStorage*);
Result appletHolderStart(AppletHolder*);
bool appletHolderCheckFinished(AppletHolder*);
Result appletHolderRequestExit(AppletHolder*);
void appletHolderClose(AppletHolder*);
