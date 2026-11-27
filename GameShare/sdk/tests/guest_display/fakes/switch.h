/* SPDX-License-Identifier: MIT -- synthetic host test declarations only. */
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint32_t Result;
typedef uint32_t u32;
enum { Module_Libnx=345, LibnxError_BadInput=1, LibnxError_NotInitialized=2, LibnxError_AlreadyInitialized=3 };
#define MAKERESULT(module,description) ((uint32_t)(module)|((uint32_t)(description)<<9))
