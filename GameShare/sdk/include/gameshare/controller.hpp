/* SPDX-License-Identifier: MIT */
#pragma once
#include <cstdint>

namespace gameshare {
enum class Button : std::uint64_t {
    A = 1ull << 0, B = 1ull << 1, X = 1ull << 2, Y = 1ull << 3,
    LeftStick = 1ull << 4, RightStick = 1ull << 5,
    L = 1ull << 6, R = 1ull << 7, ZL = 1ull << 8, ZR = 1ull << 9,
    Plus = 1ull << 10, Minus = 1ull << 11,
    Left = 1ull << 12, Up = 1ull << 13, Right = 1ull << 14, Down = 1ull << 15,
};
// Raw native Npad buttons and signed stick axes. These are independent of any
// PC gamepad protocol or local UI shortcuts. Disconnected snapshots are neutral.
struct ControllerState {
    bool connected = false;
    std::uint64_t buttons = 0;
    std::int32_t leftX = 0, leftY = 0, rightX = 0, rightY = 0;
    bool pressed(Button button) const {
        return connected && (buttons & static_cast<std::uint64_t>(button));
    }
};
} // namespace gameshare
