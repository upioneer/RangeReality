#pragma once

// BOOT button (GPIO0) UI input. Active low. Polled from loop().
// Single press: orientation toggle (parked until portrait UIs land).
// Double press: cycle built themes. Long press: reset trip clocks.
void button_init(void);
void button_poll(void);
