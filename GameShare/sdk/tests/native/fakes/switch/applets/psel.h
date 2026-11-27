#pragma once
#include <switch.h>
struct PselUserSelectionSettings {};
Result pselShowUserSelector(AccountUid*, const PselUserSelectionSettings*);
enum PselUiMode { PselUiMode_UserSelector = 0 };
struct PselUiSettings {
    struct { u8 padding[0x97] = {}; u8 unk_x97 = 0; } settings;
    u8 padding[8] = {};
};
struct PselUiReturnArg { Result res = 0; AccountUid user_id; };
static_assert(sizeof(PselUiSettings) == 0xa0 && sizeof(PselUiReturnArg) == 0x18);
Result pselUiCreate(PselUiSettings*, PselUiMode);
