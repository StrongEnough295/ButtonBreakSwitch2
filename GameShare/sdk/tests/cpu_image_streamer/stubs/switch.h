/* SPDX-License-Identifier: ISC
 * Offline synthetic test fixture. Public libounce declarations are based on
 * ouncebrew/libounce d1718bb7076fb356cb4cb81ab13d035db5367eea.
 * Copyright (c) 2026 libounce contributors (public API declarations).
 * See LICENSE and README.md for provenance and the scope of these mocks.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t Handle;
typedef uint32_t Result;
typedef uint32_t Mutex;
typedef struct { Handle revent,wevent;bool autoclear; } Event;
typedef struct { Handle session; } TipcService;
typedef struct { TipcService s;u32 capacity,generation; } GsvSync;
typedef struct { TipcService main,worker; } GsvGpu;
typedef struct { u8 data[16]; } GsvNativeSync;
typedef struct { u8 data[64]; } GsvImageLayout;
enum { Module_Libnx=345, LibnxError_BadInput=1, LibnxError_BadGfxQueueBuffer=2,
 LibnxError_NotInitialized=3,LibnxError_AlreadyInitialized=4,LibnxError_OutOfMemory=5 };
#define MAKERESULT(m,d) ((Result)((m)|((d)<<9)))
#define KERNELRESULT(name) ((Result)0xea01)
#define R_FAILED(rc) ((rc)!=0)
#define R_VALUE(rc) ((rc)&0x3fffff)
void mutexLock(Mutex*);
void mutexUnlock(Mutex*);
bool eventActive(Event*);
Result eventClear(Event*);
Result eventWait(Event*,u64);
u64 armGetSystemTick(void);
u64 armNsToTicks(u64);
u64 armTicksToNs(u64);
void armDCacheFlush(void*,size_t);
Result svcSleepThread(u64);
void gsvMakeImageLayout(GsvImageLayout*,u32,u32,u32,u32,u32,u8,uint16_t);
Result gsvGetImageSize(const GsvImageLayout*,u64*);
Result gsvGpuRegisterMemory(GsvGpu*,u64,u64,u8,u32*);
Result gsvReserveImageMemory(u8,u64,u32*);
Result gsvRegisterImageMemory(u32,u64,u64,u32,u64);
Result gsvCreateImage(const GsvImageLayout*,u32,u32*);
Result gsvCreateDisplayAlias(u32,u32);
Result gsvDestroyDisplayAlias(u32);
Result gsvDestroyImage(u32);
Result gsvReleaseImageMemory(u32);
Result gsvGpuUnregisterMemory(GsvGpu*,u32,u32);
Result gsvSyncReadCounter(GsvSync*,u32,u64*,u8*);

