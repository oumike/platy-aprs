#pragma once
#define APP_NAME "Platy APRS"   // shown to people
#define APP_ID "platy-aprs"       // protocol identifiers: no spaces
// tools/set_app_version.py defines this from the VERSION file ("v0.1.0").
#ifndef APP_VERSION
#define APP_VERSION "v0.0.0-dev"
#endif
// AX.25 destination ("tocall"). APZ* is the experimental block.
#define APRS_TOCALL "APZRHN"
