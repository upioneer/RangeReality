#pragma once
#include <Arduino.h>

// Transport-agnostic ELM327 client. The transport is any BLE link,
// serial relay, or test fake implementing send/recvLine. Parsing and
// protocol live here so truck logic is host-testable without a vehicle.

// Sim-phase custom pack commands (mode 22, scripted by obd-sim).
// Replaced by real Lightning PIDs after the truck logging trip.
#define ELM_PACK_V_CMD "226101"
#define ELM_PACK_I_CMD "226102"
#define ELM_GEAR_CMD "226103"

struct ElmTransport {
  virtual bool send(const char *cmd) = 0;
  virtual bool recvLine(char *buf, size_t n, uint32_t timeout_ms) = 0;
  virtual ~ElmTransport() {}
};

namespace elm {

// Full init: ATZ, ATE0, ATL0, ATS0, ATH1, ATSP6.
bool init(ElmTransport &t);

// Send a command, return the first data line (echo and prompt stripped).
bool query(ElmTransport &t, const char *cmd, char *resp, size_t n);

// Standard mode 01 decoders. resp is a query() data line.
bool decode01(const char *resp, uint8_t pid, float &out);

// Pack volts + amps via the sim-phase mode 22 commands.
bool packVI(ElmTransport &t, float &volts, float &amps);
// Shifter position via sim-phase 226103. Returns 'P','R','N','D', else 0.
char gear(ElmTransport &t);

}  // namespace elm
