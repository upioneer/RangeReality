#include "obd/ble_link.h"

#include <NimBLEDevice.h>
#include <lvgl.h>

#include "fw_version.h"
#include "meter.h"
#include "state.h"

// Adapter profiles: name fragment + GATT UART doorway.
// VEEPEAK entry shares the clone doorway until the truck reveals its
// real UUIDs via scan log, then it gets its own row.
struct ObdProfile {
  const char *tag;
  const char *name_frag;
  const char *svc;
  const char *tx_chr;  // we write commands here
  const char *rx_chr;  // notifies arrive here
};

static const ObdProfile kProfiles[] = {
    {"OBDSIM", "OBDSIM", "FFE0", "FFE1", "FFE1"},
    // Veepeak OBDCheck BLE (B073XKQQQW): split doorway, write FFF2,
    // notify FFF1, service FFF0.
    {"OBDCHECK_BLE", "VEEPEAK", "FFF0", "FFF2", "FFF1"},
};

static NimBLEClient *s_client = nullptr;
static NimBLERemoteCharacteristic *s_chr_tx = nullptr;
static NimBLERemoteCharacteristic *s_chr_rx = nullptr;
static bool s_have_target = false;
static NimBLEAddress s_target = NimBLEAddress("");
static const ObdProfile *s_profile = nullptr;
static bool s_inited = false;
static uint32_t s_last_attempt = 0;
static uint32_t s_last_poll = 0;
static uint8_t s_poll_step = 0;
// Link liveness: relink only when NOTHING answers, not when one sim-only
// PID fails while real data flows.
static uint32_t s_last_good_ms = 0;
// Adapter transmit header the link assumes (7DF functional default).
// Best-effort tracking: switches send ATSH, queries decide their own fate.
static char s_hdr[8] = "7DF";
static bool s_probing = false;
// Chunked discovery state: >= 0 while running, one item per loop tick.
// Full discovery runs once per boot; relinks print the session header only.
static int s_disc_step = -1;
static bool s_disc_full_done = false;
// Boot-persistent first-data markers: show whether the truck EVER answered,
// across relink storms.
static bool s_seen_pack = false;
static bool s_seen_gear = false;
static bool s_seen_spd = false;

static void probe_run(const char *const *cmds, int n);

// OBD poll period. 500 ms proved too aggressive, back to 1000.
// TODO(polish): retest faster rates against real adapter latency.
#define OBD_POLL_MS 1000

// Notify assembly buffer (BLE task context).
static char s_rx[256];
static size_t s_rx_len = 0;
static uint32_t s_notifies = 0;

static void onNotify(NimBLERemoteCharacteristic *c, uint8_t *data, size_t len, bool isNotify) {
  (void)c;
  (void)isNotify;
  s_notifies++;
  for (size_t i = 0; i < len && s_rx_len < sizeof(s_rx) - 1; i++) {
    s_rx[s_rx_len++] = (char)data[i];
  }
}

static void force_relink(void) {
  Serial.println("[ble] relink, no live data for 12 s");
  s_rx_len = 0;
  s_chr_tx = nullptr;
  s_chr_rx = nullptr;
  s_inited = false;
  if (s_client != nullptr) s_client->disconnect();
  g_ble = BleLink::SCANNING;
  s_last_attempt = millis();
}

class ScanCb : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *adv) override {
    std::string name = adv->getName();
    for (const auto &p : kProfiles) {
      bool name_hit = name.find(p.name_frag) != std::string::npos;
      bool svc_hit = adv->isAdvertisingService(NimBLEUUID(p.svc));
      if (name_hit || svc_hit) {
        Serial.printf("[ble] match %s name=%s rssi=%d\n", p.tag, name.c_str(), adv->getRSSI());
        s_target = adv->getAddress();
        s_profile = &p;
        s_have_target = true;
        NimBLEDevice::getScan()->stop();
        return;
      }
    }
    if (!name.empty()) {
      Serial.printf("[ble] seen %s rssi=%d\n", name.c_str(), adv->getRSSI());
    }
  }
};

class ClientCb : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *c) override {
    (void)c;
    Serial.println("[ble] client connected");
  }
  void onDisconnect(NimBLEClient *c) override {
    (void)c;
    Serial.println("[ble] disconnected");
    s_chr_tx = nullptr;
    s_chr_rx = nullptr;
    s_inited = false;
    g_ble = BleLink::SCANNING;
  }
};

// Keep LVGL breathing during BLE waits, at most every 20 ms. Same thread
// as loop(), so no reentrancy hazard; native unit tests never link this
// translation unit (they use a fake ElmTransport).
static void pump_ui(void) {
  static uint32_t last_pump = 0;
  uint32_t now = millis();
  if (now - last_pump >= 20) {
    last_pump = now;
    lv_timer_handler();
  }
}

struct LinkTransport : public ElmTransport {
  bool send(const char *cmd) override {
    if (s_chr_tx == nullptr) return false;
    // Fresh command, fresh buffer: drop any prompt/echo residue so a stale
    // fragment can never glue to the new reply and poison the parse.
    s_rx_len = 0;
    String full(cmd);
    full += '\r';
    Serial.printf("[obd] > %s\n", cmd);
    return s_chr_tx->writeValue(full.c_str(), false);
  }
  bool recvLine(char *buf, size_t n, uint32_t timeout_ms) override {
    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
      if (s_chr_rx == nullptr) return false;
      // Strip prompt residue: the previous reply's "\r\r>" tail strands
      // a ">" that otherwise glues to the next line and poisons it.
      while (s_rx_len > 0 && s_rx[0] == '>') {
        memmove(s_rx, s_rx + 1, s_rx_len - 1);
        s_rx_len--;
      }
      char *cr = (char *)memchr(s_rx, '\r', s_rx_len);
      if (cr != nullptr) {
        size_t len = (size_t)(cr - s_rx);
        if (len >= n) len = n - 1;
        memcpy(buf, s_rx, len);
        buf[len] = '\0';
        size_t used = (size_t)(cr - s_rx) + 1;
        memmove(s_rx, s_rx + used, s_rx_len - used);
        s_rx_len -= used;
        if (strlen(buf) == 0) continue;
        return true;
      }
      // The connected poll path (BECM slot: stacked query + header timeouts
      // up to ~5 s every 8th tick) runs before lv_timer_handler() in loop(),
      // so without this the animations freeze for the whole wait.
      pump_ui();
      delay(2);
    }
    return false;
  }
};

static LinkTransport s_transport;

static void hdr_set(const char *hdr) {
  if (strcmp(s_hdr, hdr) == 0) return;
  strncpy(s_hdr, hdr, sizeof(s_hdr) - 1);
  s_hdr[sizeof(s_hdr) - 1] = '\0';
  if (!elm::set_header(s_transport, hdr)) {
    Serial.printf("[obd] atsh %s rejected, continuing\n", hdr);
  }
}

// Adapter-local command with raw reply dump (ATRV port voltage, ATDP
// protocol). Always answers when the adapter is alive, truck or not.
static void diag_at(const char *cmd) {
  s_transport.send(cmd);
  char line[96];
  uint32_t start = millis();
  while (millis() - start < 400) {
    if (s_transport.recvLine(line, sizeof(line), 200)) {
      Serial.printf("[diag] %s -> %s\n", cmd, line);
    }
  }
}

// One discovery item per loop tick: worst case a single query budget,
// never a multi-second block. Returns true when the survey is complete.
static bool diag_step(void) {
  static const char *kDisc[] = {"0100", "0120", "0140", "015B",
                                "0902", "010C", "010D", "0142"};
  if (s_disc_step == 0) {
    diag_at("ATRV");
  } else if (s_disc_step == 1) {
    diag_at("ATDP");
  } else if (s_disc_step - 2 < (int)(sizeof(kDisc) / sizeof(kDisc[0]))) {
    const char *cmd = kDisc[s_disc_step - 2];
    char resp[96];
    if (elm::query(s_transport, cmd, resp, sizeof(resp))) {
      Serial.printf("[diag] %s -> %s (%lums)\n", cmd, resp,
                    (unsigned long)elm::last_ms);
    } else {
      Serial.printf("[diag] %s -> no reply (%lums, last: %s)\n", cmd,
                    (unsigned long)elm::last_ms, elm::last_line);
    }
  } else {
    s_disc_step = -1;
    s_disc_full_done = true;
    Serial.println("[diag] discovery done");
    return true;
  }
  s_disc_step++;
  return false;
}

static bool do_connect(void) {
  if (s_client == nullptr) {
    s_client = NimBLEDevice::createClient();
    s_client->setClientCallbacks(new ClientCb(), false);
  }
  Serial.printf("[ble] connecting %s profile=%s\n", s_target.toString().c_str(), s_profile->tag);
  if (!s_client->connect(s_target)) {
    Serial.println("[ble] connect failed");
    return false;
  }
  NimBLERemoteService *svc = s_client->getService(NimBLEUUID(s_profile->svc));
  if (svc == nullptr) {
    Serial.println("[ble] service missing");
    s_client->disconnect();
    return false;
  }
  s_chr_tx = svc->getCharacteristic(NimBLEUUID(s_profile->tx_chr));
  s_chr_rx = svc->getCharacteristic(NimBLEUUID(s_profile->rx_chr));
  if (s_chr_tx == nullptr || !s_chr_tx->canWrite()) {
    Serial.println("[ble] tx char missing or no write");
    s_chr_tx = nullptr;
    s_chr_rx = nullptr;
    s_client->disconnect();
    return false;
  }
  if (s_chr_rx == nullptr || !s_chr_rx->canNotify()) {
    Serial.println("[ble] rx char missing or no notify");
    s_chr_tx = nullptr;
    s_chr_rx = nullptr;
    s_client->disconnect();
    return false;
  }
  if (!s_chr_rx->subscribe(true, onNotify)) {
    Serial.println("[ble] subscribe failed");
    s_chr_tx = nullptr;
    s_chr_rx = nullptr;
    s_client->disconnect();
    return false;
  }
  s_rx_len = 0;
  Serial.println("[ble] subscribed, running ELM init");
  if (!elm::init(s_transport)) {
    Serial.println("[obd] ELM init failed");
    s_chr_tx = nullptr;
    s_chr_rx = nullptr;
    s_client->disconnect();
    return false;
  }
  Serial.println("[obd] ELM init OK");
  s_inited = true;
  g_ble = BleLink::CONNECTED;
  // Fresh link, no assumptions: clear any offline-demo leftovers so the UI
  // never shows a mode the truck didn't report. Live polls fill in from here.
  g_state.gear = Gear::UNKNOWN;
  g_state.kw = 0;
  g_state.soc_pct = -1.0f;
  g_state.mod_v = -1.0f;
  g_state.soc_disp = -1.0f;
  g_state.chg_w = -1;
  g_state.chg_ac_v = -1.0f;
  g_state.chg_max_kw = 200;
  g_state.chg_kwh = 0.0f;
  g_state.pace_mi = -1;
  s_poll_step = 0;
  s_last_good_ms = millis();
  s_hdr[0] = '\0';
  hdr_set("7DF");
  state_set_live_seen();
  Serial.printf("[session] fw=%s profile=%s\n", RR_FW_VERSION, s_profile->tag);
  // Full PID discovery once per boot, chunked across loop ticks so the UI
  // never blocks; relinks print the session header only.
  if (!s_disc_full_done) s_disc_step = 0;
  return true;
}

// BECM capture block (header 7E4). Community-mapped PIDs, NOT validated:
// raw replies plus both candidate current formulas are logged for the
// validation trip (compare against dash charge power). Nothing displayed.
static void poll_becm(void) {
  hdr_set("7E4");
  char resp[96];
  uint8_t d[8];
  if (elm::query(s_transport, "22480A", resp, sizeof(resp)) &&
      elm::decode22(resp, 0x480A, d, sizeof(d)) >= 2) {
    s_last_good_ms = millis();
    int raw = (d[0] << 8) | d[1];
    Serial.printf("[obd] becm cur raw=%d /336=%.1fA /100=%.1fA\n", raw, raw / 336.0,
                  (double)((int16_t)raw / 100.0f));
  } else {
    Serial.printf("[obd] becm cur failed (%lums, last: %s)\n",
                  (unsigned long)elm::last_ms, elm::last_line);
  }
  if (elm::query(s_transport, "22484E", resp, sizeof(resp)) &&
      elm::decode22(resp, 0x484E, d, sizeof(d)) >= 2) {
    s_last_good_ms = millis();
    g_state.chg_w = (long)(((d[0] << 8) | d[1]) * 5);
    charge_note_power(g_state.chg_w);
    Serial.printf("[obd] becm chg %ld W\n", g_state.chg_w);
  } else {
    Serial.printf("[obd] becm chg failed (%lums, last: %s)\n",
                  (unsigned long)elm::last_ms, elm::last_line);
  }
  if (elm::query(s_transport, "224845", resp, sizeof(resp)) &&
      elm::decode22(resp, 0x4845, d, sizeof(d)) >= 1) {
    s_last_good_ms = millis();
    g_state.soc_disp = d[0] * 0.5f;
    Serial.printf("[obd] becm socd %.1f%%\n", (double)g_state.soc_disp);
  } else {
    Serial.printf("[obd] becm socd failed (%lums, last: %s)\n",
                  (unsigned long)elm::last_ms, elm::last_line);
  }
  // AC input volts live on the charger module (7E2), not the BECM.
  hdr_set("7E2");
  if (elm::query(s_transport, "22485E", resp, sizeof(resp)) &&
      elm::decode22(resp, 0x485E, d, sizeof(d)) >= 2) {
    s_last_good_ms = millis();
    g_state.chg_ac_v = ((d[0] << 8) | d[1]) * 0.01f;
    Serial.printf("[obd] becm acv %.0fV\n", (double)g_state.chg_ac_v);
  } else {
    Serial.printf("[obd] becm acv failed (%lums, last: %s)\n",
                  (unsigned long)elm::last_ms, elm::last_line);
  }
  g_state.chg_max_kw = charge_scale_kw(g_state.chg_w, g_state.chg_ac_v);
  hdr_set("7DF");
}

static void do_poll(void) {
  Serial.printf("[obd] poll notifies=%lu rxlen=%u\n", (unsigned long)s_notifies, (unsigned)s_rx_len);
  if (s_poll_step % 8 == 3) {
    hdr_set("7DF");
    char g = elm::gear(s_transport);
    if (g == 0) {
      Serial.printf("[obd] gear query failed (%lums, last: %s)\n",
                    (unsigned long)elm::last_ms, elm::last_line);
    } else {
      s_last_good_ms = millis();
      if (!s_seen_gear) {
        s_seen_gear = true;
        Serial.println("[obd] live: gear flowing");
      }
    }
    Gear next = Gear::UNKNOWN;
    if (g == 'P') next = Gear::PARK;
    else if (g == 'R') next = Gear::REVERSE;
    else if (g == 'N') next = Gear::NEUTRAL;
    else if (g == 'D') next = Gear::DRIVE;
    if (next != Gear::UNKNOWN && next != g_state.gear) {
      g_state.gear = next;
      Serial.printf("[obd] gear %c\n", g);
    }
  } else if (s_poll_step % 8 == 7) {
    poll_becm();
  } else if (s_poll_step % 2 == 0) {
    hdr_set("7DF");
    float v, a;
    if (elm::packVI(s_transport, v, a)) {
      int kw = (int)roundf(v * a / 1000.0f);
      g_state.kw = kw;
      s_last_good_ms = millis();
      if (!s_seen_pack) {
        s_seen_pack = true;
        Serial.println("[obd] live: pack flowing");
      }
      Serial.printf("[obd] pack %.1f V %.1f A -> %d kW\n", v, a, kw);
    } else {
      Serial.printf("[obd] pack query failed (%lums, last: %s)\n",
                    (unsigned long)elm::last_ms, elm::last_line);
    }
  } else if (s_poll_step % 8 == 1) {
    hdr_set("7DF");
    char resp[96];
    float spd;
    if (elm::query(s_transport, "010D", resp, sizeof(resp)) && elm::decode01(resp, 0x0D, spd)) {
      s_last_good_ms = millis();
      g_state.speed_kmh = spd;
      if (!s_seen_spd) {
        s_seen_spd = true;
        Serial.println("[obd] live: speed flowing");
      }
      Serial.printf("[obd] speed %.0f km/h\n", (double)spd);
    } else {
      Serial.printf("[obd] speed query failed (%lums, last: %s)\n",
                    (unsigned long)elm::last_ms, elm::last_line);
    }
  } else {
    hdr_set("7DF");
    // SoC and module voltage move slowly: share the slot, alternate ticks.
    static bool volt_turn = false;
    volt_turn = !volt_turn;
    char resp[96];
    if (volt_turn) {
      float v;
      if (elm::query(s_transport, "0142", resp, sizeof(resp)) && elm::decode01(resp, 0x42, v)) {
        s_last_good_ms = millis();
        g_state.mod_v = v;
        Serial.printf("[obd] mod %.1fV\n", (double)v);
      } else {
        Serial.printf("[obd] mod query failed (%lums, last: %s)\n",
                      (unsigned long)elm::last_ms, elm::last_line);
      }
    } else {
      float soc;
      if (elm::query(s_transport, "015B", resp, sizeof(resp)) && elm::decode01(resp, 0x5B, soc)) {
        s_last_good_ms = millis();
        g_state.soc_pct = soc;
        Serial.printf("[obd] soc %.0f%%\n", (double)soc);
      } else {
        Serial.printf("[obd] soc query failed (%lums, last: %s)\n",
                      (unsigned long)elm::last_ms, elm::last_line);
      }
    }
  }
  s_poll_step++;
}

void ble_link_init(void) {
  NimBLEDevice::init("LMC");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCb());
  scan->setActiveScan(true);
  g_ble = BleLink::SCANNING;
  Serial.println("[ble] central ready");
}

static void probe_run(const char *const *cmds, int n) {
  s_probing = true;
  for (int i = 0; i < n; i++) {
    char resp[96];
    if (elm::query(s_transport, cmds[i], resp, sizeof(resp))) {
      Serial.printf("[probe] %s -> %s\n", cmds[i], resp);
    } else {
      Serial.printf("[probe] %s -> no reply\n", cmds[i]);
    }
  }
  s_probing = false;
}

void ble_raw(const char *cmd) {
  if (g_ble != BleLink::CONNECTED || !s_inited) {
    Serial.println("[raw] connect to the adapter first");
    return;
  }
  // Send one raw command and dump every reply line for 1.5 s: reveals ?,
  // NO DATA, UNABLE TO CONNECT, or real data when queries fail silently.
  s_probing = true;
  s_transport.send(cmd);
  char line[96];
  uint32_t start = millis();
  while (millis() - start < 1500) {
    if (s_transport.recvLine(line, sizeof(line), 200)) {
      Serial.printf("[raw] %s\n", line);
    }
  }
  s_probing = false;
  Serial.println("[raw] done");
}

void ble_probe(void) {
  if (g_ble != BleLink::CONNECTED || !s_inited) {
    Serial.println("[probe] connect to the adapter first");
    return;
  }
  // Standard mode-01 discovery only: support bitmasks plus a few common
  // data PIDs. Raw replies go to the log for the truck logging trip that
  // picks the real Lightning pack-power PIDs. No mode-22 guesses here.
  static const char *kPids[] = {"0100", "0120", "0140", "015B", "010C",
                                "010D", "0105", "010F", "011F", "0142", "0146"};
  probe_run(kPids, (int)(sizeof(kPids) / sizeof(kPids[0])));
  Serial.println("[probe] done");
}

void ble_link_poll(void) {
  if (s_probing) return;
  if (s_chr_tx != nullptr && s_inited) {
    if (s_client != nullptr && !s_client->isConnected()) return;
    if (s_disc_step >= 0) {
      diag_step();
      return;
    }
    if (millis() - s_last_good_ms > 12000) {
      force_relink();
      return;
    }
    if (millis() - s_last_poll > OBD_POLL_MS) {
      s_last_poll = millis();
      do_poll();
    }
    return;
  }
  if (s_have_target) {
    s_have_target = false;
    if (do_connect()) return;
    s_last_attempt = millis();
    return;
  }
  if (millis() - s_last_attempt > 4000) {
    s_last_attempt = millis();
    g_ble = BleLink::SCANNING;
    Serial.println("[ble] scanning...");
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan->isScanning()) scan->stop();
    scan->clearResults();
    // Non-blocking continuous scan: matches stream into ScanCb::onResult,
    // which stops the scan on a hit. The blocking start(duration) overload
    // stalls loop() for the whole window and freezes LVGL animation.
    scan->start(0, nullptr, true);
  }
}

const char *ble_state_str(void) {
  switch (g_ble) {
    case BleLink::CONNECTED:
      return "connected";
    case BleLink::SCANNING:
      return "scanning";
    default:
      return "off";
  }
}
