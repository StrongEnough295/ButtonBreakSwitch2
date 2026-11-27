#pragma once
#include <switch.h>
struct LibAppletArgs { u32 version = 0; };
void libappletArgsCreate(LibAppletArgs*, u32);
Result libappletArgsPush(LibAppletArgs*, AppletHolder*);
