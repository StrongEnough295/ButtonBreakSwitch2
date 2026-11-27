/* SPDX-License-Identifier: MIT */
#pragma once
#include <atomic>

namespace gameshare::detail {
// Shared by native hosts and standalone display targets. A retained renderer
// must prevent a later Host from declaring the NRO safe to unload, too.
inline std::atomic<bool> restartRequired{false};
}
