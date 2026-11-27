/* SPDX-License-Identifier: MIT
 * Minimal host stubs for the public libounce APIs used by the SDK guest_input.cpp.
 * No Switch SDK/game data is used by these tests.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef uint32_t Result;
typedef uint32_t Handle;
typedef int Permission;
enum {
    Perm_R = 1,
    Module_Libnx = 345,
    LibnxError_BadInput = 1,
    LibnxError_AlreadyInitialized = 2,
    LibnxError_NotInitialized = 3,
};
#define MAKERESULT(m, d) ((Result)((m) | ((d) << 9)))
#define R_FAILED(r) ((r) != 0)
#define R_SUCCEEDED(r) ((r) == 0)

typedef struct {
    Handle handle;
    size_t size;
    Permission perm;
    void *map_addr;
} SharedMemory;
void shmemLoadRemote(SharedMemory *, Handle, size_t, Permission);
Result shmemMap(SharedMemory *);
Result shmemUnmap(SharedMemory *);
Result shmemClose(SharedMemory *);
