#ifdef __SWITCH2__
/* SPDX-License-Identifier: MIT
 * Bounded read-only adapter for guest0 controller rings. Layout qualified from
 * the user's owned SDK (SHA2567558f288e89f81954a4d233def5f55768d67d31c00b4cc383ce9189e1f4a0dc3):
 * cmd300 config {u64 mapping_size,u64 format_offset}; FullKey/Handheld/JoyDual
 * accessors0x6b1c20/6b1d00/6b1de0, readers0x6b25c0/6b2760/6b2900, and main
 * consumers0x457fe60/4580340/457f980. Public corroborating ring
 * algorithm: vexcited/fefw,2fcc13a81864f00314b13cc83f8f5fa5035a0d3e,
 * include/nn/streamplay/detail/streamplay_ring_lifo.hpp.
 * No SDK code, IPC writer or object layout is embedded here.
 */
#include "guest_input.hpp"
#include <stdint.h>
#include <string.h>

enum {
    PAGE_SIZE = 0x1000,
    MAX_MAPPING_SIZE = 0x1000000,
    NPAD_STRIDE = 0x24000,
    FULLKEY_RING_OFFSET = 0x28,
    HANDHELD_RING_OFFSET = 0x770,
    JOYDUAL_RING_OFFSET = 0xeb8,
    RING_HEADER_SIZE = 0x10,
    RING_CAPACITY = 33,
    ENTRY_SIZE = 0x38,
    PAYLOAD_SIZE = 0x30,
    POLL_ATTEMPTS = 3,
};
static_assert(sizeof(GuestInputSample) == 48, "guest input sample ABI");
static_assert(ENTRY_SIZE == sizeof(u64) + PAYLOAD_SIZE, "guest ring entry ABI");
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "native HID is little endian");

static SharedMemory memory;
static size_t format_offset;
static u32 resources_alive;
static bool ready;

static Result badInput(void) {
    return MAKERESULT(Module_Libnx, LibnxError_BadInput);
}

/* Validate every read before forming its address. Open also verifies that
 * the entire mapped address range is representable in uintptr_t.
 */
static const volatile u8 *span(size_t offset, size_t length) {
    if (!ready || offset > memory.size || length > memory.size - offset)
        return NULL;
    return (const volatile u8 *)memory.map_addr + offset;
}

static bool formatSpan(size_t relative, size_t length, size_t *absolute) {
    if (relative > SIZE_MAX - format_offset) return false;
    *absolute = format_offset + relative;
    return span(*absolute, length) != NULL;
}

static u64 acquire64(const volatile u8 *p) {
    return __atomic_load_n((const volatile u64 *)p, __ATOMIC_ACQUIRE);
}
static u32 acquire32(const volatile u8 *p) {
    return __atomic_load_n((const volatile u32 *)p, __ATOMIC_ACQUIRE);
}

Result guestInputOpen(Handle borrowed_handle, u64 size, u64 offset) {
    if (guestInputHasResources())
        return MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
    const size_t required = 2 * NPAD_STRIDE + JOYDUAL_RING_OFFSET +
        RING_HEADER_SIZE + RING_CAPACITY * ENTRY_SIZE;
    if (!borrowed_handle || !size || size > MAX_MAPPING_SIZE || size > SIZE_MAX ||
        size % PAGE_SIZE || offset > SIZE_MAX || offset % sizeof(u64) ||
        offset > size || required > size - offset)
        return badInput();

    memset(&memory, 0, sizeof(memory));
    shmemLoadRemote(&memory, borrowed_handle, (size_t)size, Perm_R);
    Result rc = shmemMap(&memory);
    if (R_FAILED(rc)) {
        /* The public shmemMap contract leaves no mapping after failure. */
        memset(&memory, 0, sizeof(memory));
        return rc;
    }
    __atomic_store_n(&resources_alive, 1, __ATOMIC_RELEASE);
    const uintptr_t base = (uintptr_t)memory.map_addr;
    if (!base || base % sizeof(u64) || size - 1 > UINTPTR_MAX - base) {
        rc = guestInputClose();
        return R_FAILED(rc) ? rc : badInput();
    }
    format_offset = (size_t)offset;
    ready = true;
    return 0;
}

Result guestInputPoll(GuestInputSample *out) {
    if (!out) return badInput();
    memset(out, 0, sizeof(*out));
    if (!ready)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    for (unsigned attempt = 0; attempt < POLL_ATTEMPTS; ++attempt) {
        size_t style_offset;
        if (!formatSpan(0, sizeof(u32), &style_offset)) return badInput();
        u32 style = acquire32(span(style_offset, sizeof(u32)));
        size_t npad_offset = 0;
        u32 npad_id = 0;
        if (!style) {
            npad_offset = 2 * NPAD_STRIDE;
            npad_id = 0x20;
            if (!formatSpan(npad_offset, sizeof(u32), &style_offset))
                return badInput();
            style = acquire32(span(style_offset, sizeof(u32)));
        }
        out->npad_id = npad_id;
        out->style_set = style;
        if (!style) { out->status = GuestInput_NoStyle; return 0; }
        size_t selected_ring;
        if (style & 1) selected_ring = FULLKEY_RING_OFFSET;
        else if (style & 4) selected_ring = JOYDUAL_RING_OFFSET;
        else if (npad_id == 0x20 && (style & 2)) selected_ring = HANDHELD_RING_OFFSET;
        else {
            out->status = GuestInput_UnsupportedStyle;
            return 0;
        }

        size_t ring_offset;
        if (!formatSpan(npad_offset + selected_ring, RING_HEADER_SIZE,
                        &ring_offset))
            return badInput();
        const u64 count = acquire64(span(ring_offset + 8, sizeof(u64)));
        if (count > RING_CAPACITY) return badInput();
        if (!count) { out->status = GuestInput_NoSamples; return 0; }
        const u64 tail = acquire64(span(ring_offset, sizeof(u64)));
        if (tail >= RING_CAPACITY) return badInput();

        /* tail is an entry index0..32, not an unbounded sample number. */
        const size_t entry_offset = ring_offset + RING_HEADER_SIZE +
            (size_t)tail * ENTRY_SIZE;
        const volatile u8 *entry = span(entry_offset, ENTRY_SIZE);
        if (!entry) return badInput();
        const u64 before = acquire64(entry);
        if (before & 1) continue;

        u64 payload[PAYLOAD_SIZE / sizeof(u64)];
        for (size_t i = 0; i < PAYLOAD_SIZE / sizeof(u64); ++i) {
            const volatile u8 *word = span(entry_offset + 8 + i * sizeof(u64),
                                           sizeof(u64));
            if (!word) return badInput();
            payload[i] = __atomic_load_n((const volatile u64 *)word,
                                         __ATOMIC_RELAXED);
        }
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const u64 after = __atomic_load_n((const volatile u64 *)entry,
                                          __ATOMIC_RELAXED);
        const u32 current_style = acquire32(span(style_offset, sizeof(u32)));
        if (before != after || current_style != style) continue;

        const u8 *bytes = (const u8 *)payload;
        memcpy(&out->sampling_number, bytes + 8, sizeof(out->sampling_number));
        memcpy(&out->buttons, bytes + 16, sizeof(out->buttons));
        memcpy(&out->left_x, bytes + 24, sizeof(out->left_x));
        memcpy(&out->left_y, bytes + 28, sizeof(out->left_y));
        memcpy(&out->right_x, bytes + 32, sizeof(out->right_x));
        memcpy(&out->right_y, bytes + 36, sizeof(out->right_y));
        memcpy(&out->attributes, bytes + 40, sizeof(out->attributes));
        out->status = GuestInput_Ready;
        return 0;
    }
    out->status = GuestInput_Unstable;
    return 0;
}

Result guestInputClose(void) {
    if (!guestInputHasResources()) return 0;
    ready = false;
    Result rc = shmemUnmap(&memory);
    if (R_FAILED(rc)) return rc;
    /* shmemClose would also close the caller's borrowed handle. Never use it. */
    memset(&memory, 0, sizeof(memory));
    format_offset = 0;
    __atomic_store_n(&resources_alive, 0, __ATOMIC_RELEASE);
    return 0;
}

bool guestInputHasResources(void) {
    return __atomic_load_n(&resources_alive, __ATOMIC_ACQUIRE) != 0;
}

#endif
