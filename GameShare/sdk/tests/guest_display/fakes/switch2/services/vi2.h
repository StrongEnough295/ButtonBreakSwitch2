/* SPDX-License-Identifier: MIT -- public VI2 ABI, synthetic service operations. */
#pragma once
#include <switch.h>
typedef struct {uint64_t name;uint32_t notification_index;} Vi2Display;
typedef struct {Vi2Display* display;uint64_t name;} Vi2Endpoint;
typedef struct {uint64_t token;uint32_t destroy_notification,availability_notification;} Vi2Layer;
Result vi2Initialize(void);
void vi2Exit(void);
Result vi2OpenDisplay(Vi2Display*,const char*);
Result vi2CloseDisplay(Vi2Display*);
void vi2CreateEndpoint(Vi2Endpoint*,Vi2Display*,const char*);
void vi2CloseEndpoint(Vi2Endpoint*);
Result vi2CreateLayer(Vi2Layer*);
Result vi2AttachLayer(Vi2Layer*,Vi2Display*);
Result vi2SelectLayer(Vi2Layer*,Vi2Endpoint*);
Result vi2DetachLayer(Vi2Layer*);
Result vi2ClearLayerSelection(Vi2Endpoint*);
Result vi2CloseLayer(Vi2Layer*);
