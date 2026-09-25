#pragma once
#include "obd/elm.h"

// BLE central: finds an OBD adapter by profile, holds the ELM327
// session, and feeds pack data into g_state. No phone needed.

void ble_link_init(void);
// Call from loop(). Short blocking bursts only.
void ble_link_poll(void);
// Manual PID discovery: log raw replies to standard mode-01 PIDs.
// Only runs while connected; the periodic poll pauses until it finishes.
void ble_probe(void);
const char *ble_state_str(void);
