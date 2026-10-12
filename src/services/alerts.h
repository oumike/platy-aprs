#pragma once

// Short buzzer patterns, played without blocking. Silent when the buzzer is
// turned off in the config.
enum class Alert { Message, Ack, Failed, Beacon };
void alertPlay(Alert a);
void alertsLoop();   // call from loop()
