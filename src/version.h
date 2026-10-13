#pragma once
#define APP_NAME "platy-aprs"
// Release builds override this with -DAPP_VERSION=\"x.y.z\".
#ifndef APP_VERSION
#define APP_VERSION "0.1.0-dev"
#endif
// AX.25 destination ("tocall"). APZ* is the experimental block.
#define APRS_TOCALL "APZRHN"
