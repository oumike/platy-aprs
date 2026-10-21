#pragma once
#include <Arduino.h>

// Over-the-air updates, ported from camillia-mt's ota_update.cpp (without its
// alpha channel). Releases come over plain HTTP from ota.platy.sumat.org, a
// proxy for this repo's GitHub releases (base = http://ota.platy.sumat.org/firmware):
//   {base}/latest                                  release JSON (tag_name)
//   {base}/<tag>/platy-aprs-<board>-<tag>-ota.bin  the app image
//   {base}/<tag>/...-ota.bin.sig                   detached ECDSA P-256 signature
// The image is hashed as it streams and the signature checked against the
// public key baked into the firmware (ota_signing_pubkey.h) before the new
// slot is made bootable, which is what makes the TLS-free download safe.
//
// Checks and installs run on a worker task; the UI and web poll otaStatus().
// otaLoop() checks 20 s after joining Wi-Fi and then daily, and installs on its
// own when g_config.otaAutoInstall is set.

enum class OtaState : uint8_t { Idle, Checking, UpToDate, Available, Installing, Done, Failed };

struct OtaStatus {
    OtaState state = OtaState::Idle;
    String latest;        // newest release tag, once a check has succeeded
    String error;         // set in Failed
    size_t written = 0, total = 0;   // install progress (total 0 = unknown)
    bool layoutOk = true;            // false on a flash layout OTA must not touch
};

void otaBegin();
void otaLoop();
OtaStatus otaStatus();
const char *otaStateName(OtaState s);
bool otaStartCheck();     // false when busy, not on Wi-Fi, or unsupported
bool otaStartInstall();   // installs the latest release; reboots when it is done
const char *otaAssetSlug();
