#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void LightingRemoteInit(void);
void LightingRemoteOpenPairingWindow(uint32_t durationMs);

bool LightingRemoteIsPairingWindowOpen(void);
int LightingRemoteIsAuthorized(uint16_t remoteId);
int LightingRemoteHasPairs(void);
int LightingRemoteIsPaired(uint16_t remoteId);
void LightingRemotePairRequest(uint16_t remoteId);
int LightingRemoteClearPair(uint16_t remoteId);
void LightingRemoteFirstUsedNotify(void);
void LightingRemoteEnableAfterCommissioning(void);
bool LightingRemoteShouldDeferPowerOnDefaults(void);

#ifdef __cplusplus
}
#endif
