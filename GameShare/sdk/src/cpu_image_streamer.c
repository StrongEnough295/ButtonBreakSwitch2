/* SPDX-License-Identifier: ISC
 * Guest CPU RGBA8 image uploader adapted from ouncebrew/libounce at
 * d1718bb7076fb356cb4cb81ab13d035db5367eea:
 * nx/source/switch2/display/cpu_window.{c,h}, blobs
 * 41218d8ce93ef688c918ed0de3032412c8ff9f70 /
 * e34d1415cc1b73424631c4a6e8b35906d186fb15.
 *
 * Copyright (c) 2026 libounce contributors
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "cpu_image_streamer.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <switch2/kernel/svc.h>

/* Exact private CPU backend types from the pinned header. No object/handle is
 * shallow-copied: only the existing owner is borrowed through this typed ABI.
 */
typedef struct {
    u32 memory_id;
    u32 image_id;
    Vi2Dependency release;
} CpuWindowImage;
typedef struct {
    bool is_open;
    GsvSync sync;
    GsvGpu gpu;
    Event slot_event;
    u8 client;
    u32 as_fd;
    Vi2Layer *layer;
    void *backing;
    u64 backing_size;
    u32 range_id, width, height, num_images;
    CpuWindowImage images[3];
} CpuWindow;
extern CpuWindow *cpuwinGetDefault(void);
extern bool cpuwinDefaultHasImages(void);
_Static_assert(sizeof(GsvSync) == 12 && sizeof(GsvGpu) == 8 && sizeof(Event) == 12,
               "Pinned libounce graphics handle ABI changed");
_Static_assert(offsetof(CpuWindow, sync) == 4 && offsetof(CpuWindow, gpu) == 16 &&
               offsetof(CpuWindow, slot_event) == 24 && offsetof(CpuWindow, layer) == 48 &&
               offsetof(CpuWindow, images) == 88 && sizeof(CpuWindow) == 160,
               "Pinned libounce CpuWindow ABI changed");

#define IMAGE_COUNT 2
#define WAIT_NS UINT64_C(2000000000)
#define POLL_NS UINT64_C(1000000)
#define BACKING_ATTRIBUTE 0x20
#define WORKER_SLOT 0
#define PITCH ((GS_CPU_IMAGE_WIDTH * 4u + 63u) & ~63u)
#define EXPECTED_IMAGE_SIZE ((u64)PITCH * ((GS_CPU_IMAGE_HEIGHT + 127u) & ~127u))
_Static_assert((EXPECTED_IMAGE_SIZE * IMAGE_COUNT) % 0x10000 == 0,
               "aligned_alloc requires an alignment-multiple allocation");

typedef struct {
    u32 memory_id, image_id;
    bool memory_owned, image_owned, alias_owned;
    Vi2Dependency release;
} GuestImage;
typedef struct {
    CpuWindow *owner;
    Vi2Layer *layer;
    Vi2Layer layer_identity;
    Handle gpu_main, gpu_worker, sync, slot_event;
    u32 sync_generation;
    u8 client;
    void *backing;
    u64 image_size, backing_size, frame;
    u32 range_id, next_image;
    bool attribute_set, range_owned, range_pending;
    bool ready, closing, presented, layer_retired;
    Result quarantine;
    GuestImage images[IMAGE_COUNT];
} GuestRenderer;
static GuestRenderer renderer;
static Mutex renderer_mutex;
static u32 resources_alive;

static Result badInput(void) { return MAKERESULT(Module_Libnx, LibnxError_BadInput); }
static Result badRelease(void) { return MAKERESULT(Module_Libnx, LibnxError_BadGfxQueueBuffer); }
static Result quarantine(Result rc) {
    if (!renderer.quarantine) renderer.quarantine = rc ? rc : badRelease();
    return renderer.quarantine;
}
static bool ownerUnchanged(void) {
    CpuWindow *w = renderer.owner;
    return w && w->is_open && w->num_images && cpuwinDefaultHasImages() &&
        w->gpu.main.session == renderer.gpu_main &&
        w->gpu.worker.session == renderer.gpu_worker &&
        w->sync.s.session == renderer.sync && w->sync.generation == renderer.sync_generation &&
        w->slot_event.revent == renderer.slot_event && w->client == renderer.client &&
        renderer.layer && renderer.layer->token == renderer.layer_identity.token &&
        renderer.layer->destroy_notification == renderer.layer_identity.destroy_notification &&
        renderer.layer->availability_notification == renderer.layer_identity.availability_notification;
}
static bool releaseEmpty(const Vi2Dependency *release) {
    u32 token; u64 threshold;
    memcpy(&token, release->data, sizeof(token));
    memcpy(&threshold, release->data + 8, sizeof(threshold));
    return !token && !release->data[4] && !threshold;
}
static Result waitRelease(const Vi2Dependency *release) {
    if (releaseEmpty(release)) return 0;
    if (release->data[4] != 1) return quarantine(badRelease());
    u32 token; u64 threshold;
    memcpy(&token, release->data, sizeof(token));
    memcpy(&threshold, release->data + 8, sizeof(threshold));
    const u64 start = armGetSystemTick();
    for (;;) {
        u64 value = 0; u8 mode = 0;
        Result rc = gsvSyncReadCounter(&renderer.owner->sync, ~token, &value, &mode);
        if (R_FAILED(rc)) return rc;
        if (mode != 1) return quarantine(badRelease());
        if (value >= threshold) return 0;
        if (armGetSystemTick() - start >= armNsToTicks(WAIT_NS)) return KERNELRESULT(TimedOut);
        svcSleepThread(POLL_NS);
    }
}
static Result acquireSlot(u8 *out) {
    const u64 start = armGetSystemTick();
    Result rc = vi2WaitLayerAvailability(renderer.layer, 0);
    if (R_FAILED(rc) && R_VALUE(rc) != KERNELRESULT(TimedOut)) return rc;
    for (;;) {
        u64 available = 0;
        rc = vi2GetLayerAvailability(renderer.layer, &available);
        if (R_FAILED(rc)) return rc;
        if (available) { *out = (u8)__builtin_ctzll(available); return 0; }
        u64 elapsed = armGetSystemTick() - start;
        if (elapsed >= armNsToTicks(WAIT_NS)) return KERNELRESULT(TimedOut);
        u64 left = WAIT_NS - armTicksToNs(elapsed);
        if (left > 100 * POLL_NS) left = 100 * POLL_NS;
        rc = vi2WaitLayerAvailability(renderer.layer, left);
        if (R_FAILED(rc) && R_VALUE(rc) != KERNELRESULT(TimedOut)) return rc;
    }
}

/* Turing Color2D:64x8-byte GOBs,16 GOBs/128rows per block. R is the low byte. */
static u32 pixelOffset(u32 x, u32 y) {
    const u32 xb = x * 4, gx = xb % 64, gy = y % 8;
    const u32 block = (y / 128) * (PITCH / 64) + xb / 64;
    return block * 8192 + ((y % 128) / 8) * 512 + (gx / 32) * 256 +
        (gy / 4) * 128 + ((gx % 32) / 16) * 64 + (gy % 4) * 16 + gx % 16;
}
static void copyRgba8(u8 *image, const void *source, size_t stride) {
    const u8 *input = source;
    for (u32 y = 0; y < GS_CPU_IMAGE_HEIGHT; y++)
        for (u32 x = 0; x < GS_CPU_IMAGE_WIDTH; x++)
            memcpy(image + pixelOffset(x, y), input + (size_t)y * stride + x * 4u, 4);
}

Result gsCpuImageStreamerOpen(Vi2Layer *layer) {
    mutexLock(&renderer_mutex);
    Result rc = 0;
    if (resources_alive) { rc = MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized); goto done; }
    /* The non-opening predicate must precede GetDefault, whose lazy path could
     * initialize graphics. We borrow only an already live console context.
     */
    if (!layer || !layer->token || !cpuwinDefaultHasImages()) {
        rc = MAKERESULT(Module_Libnx, LibnxError_NotInitialized); goto done;
    }
    CpuWindow *owner = cpuwinGetDefault();
    if (!owner || !owner->is_open || !owner->num_images || !owner->layer ||
        owner->layer->token == layer->token || !eventActive(&owner->slot_event) ||
        !owner->gpu.main.session || !owner->gpu.worker.session || !owner->sync.s.session) {
        rc = badInput(); goto done;
    }
    memset(&renderer, 0, sizeof(renderer));
    renderer.owner = owner; renderer.layer = layer; renderer.layer_identity = *layer;
    renderer.gpu_main = owner->gpu.main.session; renderer.gpu_worker = owner->gpu.worker.session;
    renderer.sync = owner->sync.s.session; renderer.sync_generation = owner->sync.generation;
    renderer.slot_event = owner->slot_event.revent; renderer.client = owner->client;
    GsvImageLayout layout;
    gsvMakeImageLayout(&layout, GS_CPU_IMAGE_WIDTH, GS_CPU_IMAGE_HEIGHT, 7, PITCH / 64, 1, 1, 0);
    rc = gsvGetImageSize(&layout, &renderer.image_size);
    if (R_FAILED(rc)) goto done;
    if (renderer.image_size != EXPECTED_IMAGE_SIZE) { rc = badInput(); goto done; }
    renderer.backing_size = renderer.image_size * IMAGE_COUNT;
    renderer.backing = aligned_alloc(0x10000, (size_t)renderer.backing_size);
    if (!renderer.backing) { rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory); goto done; }
    __atomic_store_n(&resources_alive, 1, __ATOMIC_RELEASE);
    memset(renderer.backing, 0, (size_t)renderer.backing_size);
    rc = svcUnknown80(renderer.backing, renderer.backing_size, BACKING_ATTRIBUTE, BACKING_ATTRIBUTE);
    if (R_FAILED(rc)) {
        free(renderer.backing); memset(&renderer, 0, sizeof(renderer));
        __atomic_store_n(&resources_alive, 0, __ATOMIC_RELEASE); goto done;
    }
    renderer.attribute_set = true;
    rc = gsvGpuRegisterMemory(&owner->gpu, (uintptr_t)renderer.backing,
        renderer.backing_size, owner->client, &renderer.range_id);
    if (R_FAILED(rc) || !renderer.range_id) { rc = quarantine(rc ? rc : badInput()); goto done; }
    renderer.range_owned = true;
    for (u32 i = 0; i < IMAGE_COUNT; i++) {
        GuestImage *image = &renderer.images[i];
        rc = gsvReserveImageMemory(3, renderer.image_size, &image->memory_id);
        if (R_FAILED(rc)) { rc = quarantine(rc); goto done; }
        image->memory_owned = true;
        rc = gsvRegisterImageMemory(image->memory_id,
            (uintptr_t)renderer.backing + i * renderer.image_size,
            renderer.image_size, renderer.range_id, i * renderer.image_size);
        if (R_FAILED(rc)) { rc = quarantine(rc); goto done; }
        rc = gsvCreateImage(&layout, image->memory_id, &image->image_id);
        if (R_FAILED(rc)) { rc = quarantine(rc); goto done; }
        image->image_owned = true;
        rc = gsvCreateDisplayAlias(image->image_id, 1);
        if (R_FAILED(rc)) { rc = quarantine(rc); goto done; }
        image->alias_owned = true;
    }
    renderer.ready = true;
done:
    mutexUnlock(&renderer_mutex);
    return rc;
}

Result gsCpuImageStreamerPresentRgba8(const void *source, size_t stride, u32 width, u32 height) {
    if (!source || width != GS_CPU_IMAGE_WIDTH || height != GS_CPU_IMAGE_HEIGHT ||
        stride < (size_t)GS_CPU_IMAGE_WIDTH * 4 ||
        stride > (SIZE_MAX - (size_t)GS_CPU_IMAGE_WIDTH * 4) / (GS_CPU_IMAGE_HEIGHT - 1))
        return badInput();
    const size_t span = (size_t)(GS_CPU_IMAGE_HEIGHT - 1) * stride + (size_t)GS_CPU_IMAGE_WIDTH * 4;
    if ((uintptr_t)source > UINTPTR_MAX - span) return badInput();
    mutexLock(&renderer_mutex);
    Result rc = 0;
    if (renderer.quarantine) { rc = renderer.quarantine; goto done; }
    if (!resources_alive || !renderer.ready || renderer.closing) {
        rc = MAKERESULT(Module_Libnx, LibnxError_NotInitialized); goto done;
    }
    if (!ownerUnchanged()) { rc = quarantine(badInput()); goto done; }
    GuestImage *image = &renderer.images[renderer.next_image];
    rc = waitRelease(&image->release); if (R_FAILED(rc)) goto done;
    u8 slot;
    rc = acquireSlot(&slot); if (R_FAILED(rc)) goto done;
    u8 *pixels = (u8 *)renderer.backing + renderer.next_image * renderer.image_size;
    copyRgba8(pixels, source, stride);
    armDCacheFlush(pixels, (size_t)renderer.image_size);
    Vi2PresentOptions options;
    vi2MakeBasicPresentOptions(&options, GS_CPU_IMAGE_WIDTH, GS_CPU_IMAGE_HEIGHT, armGetSystemTick());
    const Vi2Dependency producer = {0};
    Vi2Dependency release = {0}; u64 available = 0;
    /* From this point an unsuccessful/uncertain reply cannot justify freeing
     * the image: VI2 may have accepted it without giving us its release.
     */
    renderer.presented = true;
    rc = vi2PresentLayer(renderer.layer, slot, image->image_id, &options, &producer, &available, &release);
    if (R_FAILED(rc) || release.data[4] != 1) { rc = quarantine(rc ? rc : badRelease()); goto done; }
    image->release = release;
    renderer.frame++; renderer.next_image = (renderer.next_image + 1) % IMAGE_COUNT;
done:
    mutexUnlock(&renderer_mutex);
    return rc;
}

Result gsCpuImageStreamerClose(void) {
    mutexLock(&renderer_mutex);
    Result rc = 0;
    if (!resources_alive) goto done;
    if (renderer.quarantine) { rc = renderer.quarantine; goto done; }
    if (!ownerUnchanged()) { rc = quarantine(badInput()); goto done; }
    renderer.closing = true;
    if (renderer.presented && !renderer.layer_retired) {
        u8 slot; rc = acquireSlot(&slot); if (R_FAILED(rc)) goto done;
        Vi2PresentOptions options;
        vi2MakeBasicPresentOptions(&options, GS_CPU_IMAGE_WIDTH, GS_CPU_IMAGE_HEIGHT, armGetSystemTick());
        options.flags &= ~UINT64_C(1);
        Vi2Dependency release = {0}; u64 available = 0;
        rc = vi2UnregisterLayerImage(renderer.layer, slot, &options, &available, &release);
        if (R_FAILED(rc) || !releaseEmpty(&release)) { rc = quarantine(rc ? rc : badRelease()); goto done; }
        renderer.layer_retired = true;
    }
    for (u32 i = 0; i < IMAGE_COUNT; i++) {
        rc = waitRelease(&renderer.images[i].release); if (R_FAILED(rc)) goto done;
        memset(&renderer.images[i].release, 0, sizeof(renderer.images[i].release));
    }
    for (u32 i = 0; i < IMAGE_COUNT; i++) {
        GuestImage *image = &renderer.images[i];
        if (image->alias_owned) {
            rc = gsvDestroyDisplayAlias(image->image_id);
            if (R_FAILED(rc)) { rc = quarantine(rc); goto done; }
            image->alias_owned = false;
        }
        if (image->image_owned) {
            rc = gsvDestroyImage(image->image_id);
            if (R_FAILED(rc)) { rc = quarantine(rc); goto done; }
            image->image_owned = false;
        }
        if (image->memory_owned) {
            rc = gsvReleaseImageMemory(image->memory_id);
            if (R_FAILED(rc)) { rc = quarantine(rc); goto done; }
            image->memory_owned = false;
        }
    }
    if (renderer.range_owned) {
        if (!renderer.range_pending) {
            /* Slot0 is exclusively used for this range retirement until its
             * event signals. Console presentation does not use that slot.
             */
            rc = eventClear(&renderer.owner->slot_event); if (R_FAILED(rc)) goto done;
            rc = gsvGpuUnregisterMemory(&renderer.owner->gpu, renderer.range_id, WORKER_SLOT);
            if (R_FAILED(rc)) { rc = quarantine(rc); goto done; }
            renderer.range_pending = true;
        }
        rc = eventWait(&renderer.owner->slot_event, WAIT_NS);
        if (R_FAILED(rc)) goto done;
        renderer.range_pending = false; renderer.range_owned = false;
    }
    if (renderer.attribute_set) {
        rc = svcUnknown80(renderer.backing, renderer.backing_size, BACKING_ATTRIBUTE, 0);
        if (R_FAILED(rc)) goto done;
        renderer.attribute_set = false;
    }
    free(renderer.backing);
    memset(&renderer, 0, sizeof(renderer));
    __atomic_store_n(&resources_alive, 0, __ATOMIC_RELEASE);
done:
    mutexUnlock(&renderer_mutex);
    return rc;
}

bool gsCpuImageStreamerHasResources(void) {
    return __atomic_load_n(&resources_alive, __ATOMIC_ACQUIRE) != 0;
}
