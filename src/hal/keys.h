#pragma once
// Board-independent key codes fed to the UI. They are the M9 keyboard
// controller's own codes; other boards translate their input into them.
#define RK_ENTER       0x0D
#define RK_BKSP        0x08
#define RK_TAB         0x09   // next field
#define RK_LEFT        0xB4
#define RK_UP          0xB5
#define RK_DOWN        0xB6
#define RK_RIGHT       0xB7
#define RK_MESSAGE     0x81
#define RK_HOME        0x82
#define RK_MONITOR     0x83   // M9: function button below Home
#define RK_SUB_BACK    0x84   // M9: function button below Back (Stations)
#define RK_MAP         0x85   // M9: Map button (station heat map)
#define RK_BACK        0x86
