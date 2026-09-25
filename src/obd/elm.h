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

// Truck-trip diagnostics: most recent non-echo reply line seen by query(),
// and how long that query took in ms. Empty/zero when nothing arrived.
extern char last_line[96];
extern uint32_t last_ms;

// Pack volts + amps via the sim-phase mode 22 commands.
bool packVI(ElmTransport &t, float &volts, float &amps);
// Shifter position via sim-phase 226103. Returns 'P','R','N','D', else 0.
char gear(ElmTransport &t);
// Switch the adapter transmit header (ATSH<hdr>, e.g. 7E4 for the BECM).
// Best-effort: false when the far end rejects it, queries decide their fate.
bool set_header(ElmTransport &t, const char *hdr);
// Mode-22 payload bytes for a 2-byte pid from a query() data line
// (headers already stripped). Returns payload length or -1 on mismatch.
int decode22(const char *resp, uint16_t pid, uint8_t *out, int max);

}  // namespace elm
