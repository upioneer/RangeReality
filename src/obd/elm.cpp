#include "obd/elm.h"

namespace elm {
namespace {

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Parse "62 61 01 05 5F 00" style lines into bytes. Stops at '>'.
int parseBytes(const char *line, uint8_t *out, int max) {
  int n = 0;
  while (*line != '\0' && n < max) {
    while (*line == ' ') line++;
    if (*line == '\0' || *line == '>') break;
    int hi = hexVal(line[0]);
    int lo = hexVal(line[1]);
    if (hi < 0 || lo < 0) break;
    out[n++] = (uint8_t)((hi << 4) | lo);
    line += 2;
  }
  return n;
}

bool expectOk(ElmTransport &t, const char *cmd) {
  char line[64];
  if (!t.send(cmd)) return false;
  uint32_t start = millis();
  while (millis() - start < 1500) {
    if (!t.recvLine(line, sizeof(line), 300)) continue;
    if (strstr(line, "OK") != nullptr) return true;
    if (strchr(line, '>') != nullptr) return false;
  }
  return false;
}

}  // namespace

char last_line[96] = {0};
uint32_t last_ms = 0;

bool init(ElmTransport &t) {
  char line[64];
  // ATZ resets; answer is a version string, not OK.
  if (!t.send("ATZ")) return false;
  bool sawVersion = false;
  uint32_t start = millis();
  while (millis() - start < 2500) {
    if (!t.recvLine(line, sizeof(line), 500)) continue;
    if (strstr(line, "ELM327") != nullptr) sawVersion = true;
    if (strchr(line, '>') != nullptr) break;
  }
  if (!sawVersion) return false;
  const char *setup[] = {"ATE0", "ATL0", "ATS0", "ATH1", "ATSP6"};
  for (const char *cmd : setup) {
    if (!expectOk(t, cmd)) return false;
  }
  return true;
}

bool query(ElmTransport &t, const char *cmd, char *resp, size_t n) {
  char line[96];
  if (!t.send(cmd)) return false;
  // Poll-path budget: a healthy CAN reply arrives in milliseconds, so fail
  // fast and let the next tick retry instead of stalling the UI loop.
  last_line[0] = '\0';
  uint32_t start = millis();
  last_ms = 0;
  while (millis() - start < 600) {
    if (!t.recvLine(line, sizeof(line), 150)) continue;
    if (strchr(line, '>') != nullptr && strlen(line) < 3) continue;
    // Skip the echo of our own command.
    char echo[32];
    snprintf(echo, sizeof(echo), "%s", cmd);
    for (char *p = echo; *p != '\0'; p++) {
      if (*p == ' ') memmove(p, p + 1, strlen(p));
    }
    char nospace[96];
    size_t j = 0;
    for (size_t i = 0; line[i] != '\0' && j < sizeof(nospace) - 1; i++) {
      if (line[i] != ' ') nospace[j++] = line[i];
    }
    nospace[j] = '\0';
    if (strcmp(nospace, echo) == 0) continue;
    strncpy(last_line, line, sizeof(last_line) - 1);
    last_line[sizeof(last_line) - 1] = '\0';
    // Payload may carry an 11-bit CAN header (truck with ATH1: HHHLL plus
    // payload, e.g. 7EF03410D00) or none (sim). Strip HHHLL to the 41/62
    // payload; anything else falls through to the NO DATA/? checks.
    const char *data = line;
    if (strncmp(data, "41", 2) != 0 && strncmp(data, "62", 2) != 0) {
      bool headed = strlen(data) >= 9;
      for (int i = 0; i < 5 && headed; i++) headed = hexVal(data[i]) >= 0;
      headed = headed && (strncmp(data + 5, "41", 2) == 0 || strncmp(data + 5, "62", 2) == 0);
      if (headed) {
        data += 5;
      } else {
        if (strstr(line, "?") != nullptr || strstr(line, "NO DATA") != nullptr) {
          last_ms = millis() - start;
          return false;
        }
        continue;
      }
    }
    {
      strncpy(resp, data, n - 1);
      resp[n - 1] = '\0';
      // Drain to the prompt.
      while (millis() - start < 600) {
        if (!t.recvLine(line, sizeof(line), 100)) break;
        if (strchr(line, '>') != nullptr) break;
      }
      last_ms = millis() - start;
      return true;
    }
  }
  last_ms = millis() - start;
  return false;
}

bool decode01(const char *resp, uint8_t pid, float &out) {
  uint8_t b[8];
  int n = parseBytes(resp, b, 8);
  if (n < 3 || b[0] != 0x41 || b[1] != pid) return false;
  switch (pid) {
    case 0x0C:
      if (n < 4) return false;
      out = ((b[2] << 8) | b[3]) / 4.0f;
      return true;
    case 0x0D:
      out = b[2];
      return true;
    case 0x42:
      if (n < 4) return false;
      out = ((b[2] << 8) | b[3]) / 1000.0f;
      return true;
    case 0x5B:
      out = b[2] * 100.0f / 255.0f;
      return true;
    default:
      return false;
  }
}

bool set_header(ElmTransport &t, const char *hdr) {
  char cmd[16];
  snprintf(cmd, sizeof(cmd), "ATSH%s", hdr);
  return expectOk(t, cmd);
}

int decode22(const char *resp, uint16_t pid, uint8_t *out, int max) {
  uint8_t b[16];
  int n = parseBytes(resp, b, 16);
  if (n < 4 || b[0] != 0x62) return -1;
  if (b[1] != ((pid >> 8) & 0xFF) || b[2] != (pid & 0xFF)) return -1;
  int m = n - 3;
  if (m > max) m = max;
  memcpy(out, b + 3, (size_t)m);
  return m;
}

char gear(ElmTransport &t) {
  char resp[96];
  uint8_t b[8];
  if (!query(t, ELM_GEAR_CMD, resp, sizeof(resp))) return 0;
  if (parseBytes(resp, b, 8) < 4 || b[0] != 0x62 || b[1] != 0x61 || b[2] != 0x03) return 0;
  char g = (char)b[3];
  return (g == 'P' || g == 'R' || g == 'N' || g == 'D') ? g : 0;
}

bool packVI(ElmTransport &t, float &volts, float &amps) {
  char resp[96];
  uint8_t b[8];
  if (!query(t, ELM_PACK_V_CMD, resp, sizeof(resp))) return false;
  if (parseBytes(resp, b, 8) < 6 || b[0] != 0x62 || b[1] != 0x61 || b[2] != 0x01) return false;
  uint32_t mv = ((uint32_t)b[3] << 16) | ((uint32_t)b[4] << 8) | b[5];
  volts = mv / 1000.0f;
  if (!query(t, ELM_PACK_I_CMD, resp, sizeof(resp))) return false;
  if (parseBytes(resp, b, 8) < 6 || b[0] != 0x62 || b[1] != 0x61 || b[2] != 0x02) return false;
  int32_t ma = ((int32_t)b[3] << 16) | ((int32_t)b[4] << 8) | b[5];
  if (ma & 0x800000) ma |= 0xFF000000;
  amps = ma / 1000.0f;
  return true;
}

}  // namespace elm
