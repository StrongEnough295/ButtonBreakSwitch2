// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#pragma once
bool Switch2GameShareAvailable();
void Switch2GameShareToggle();
void Switch2GameSharePoll();
// Requests shutdown and returns true only when ordinary renderer teardown is safe.
bool Switch2GameSharePrepareExit();
