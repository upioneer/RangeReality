#include <Arduino.h>
#include <NimBLEDevice.h>
#include <LovyanGFX.hpp>

// Desk simulator for the OBD2 BLE link. Flash to the spare board with
// the obd-sim env. Status screen is boot proof; serial carries the
// command log. Service/char mirror common ELM327 BLE clones: FFE0 / FFE1.
// Serial log format (> command, < response) doubles as replay corpus.

#define SIM_SVC "FFE0"
#define SIM_CHR "FFE1"

// Same ST7789 CYD setup as the main unit. The spare is a different
// manufacturing run, so if this shows mirrored or shifted text the sim
// still works: BLE and serial do not depend on the panel.
class LGFX_SIM : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

 public:
  LGFX_SIM(void) {
    {
      auto cfg = _bus.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc = 2;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

static LGFX_SIM s_tft;
static String s_last_cmd = "-";

static NimBLEServer *s_server = nullptr;
static NimBLECharacteristic *s_chr = nullptr;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;

static void notify(const std::string &s) {
  if (s_conn == BLE_HS_CONN_HANDLE_NONE || s_chr == nullptr) return;
  s_chr->setValue((uint8_t *)s.c_str(), s.length());
  s_chr->notify();
}

static void answer(const String &s) {
  Serial.printf("< %s\n", s.c_str());
  std::string out(s.c_str());
  out += "\r\r>";
  notify(out);
}

// Scripted pack state (theatrical values for UI exercise, not truck data).
// Drive: 20 s hard discharge, 10 s regen. Park window: DC fast charge.
static void packScript(float &volts, float &amps) {
  uint32_t tsec = millis() / 1000;
  volts = 352.0f + sinf(millis() / 20000.0f) * 2.0f;
  if ((tsec % 60) >= 45) {
    amps = 500.0f;
    return;
  }
  uint32_t phase = tsec % 30;
  amps = (phase < 20) ? 1100.0f : -250.0f;
}

static void screen_draw(void) {
  s_tft.fillScreen(TFT_BLACK);
  s_tft.setTextSize(2);
  s_tft.setTextColor(TFT_WHITE, TFT_BLACK);
  s_tft.setCursor(10, 10);
  s_tft.println("OBDSIM 0.1");
  s_tft.setTextSize(1);
  s_tft.printf("up %lus\n", (unsigned long)(millis() / 1000));
  s_tft.printf("ble %s\n", s_conn == BLE_HS_CONN_HANDLE_NONE ? "ADV" : "CONN");
  s_tft.printf("last %s\n", s_last_cmd.c_str());
  float v, a;
  packScript(v, a);
  s_tft.printf("pack %.0fV %+.0fA\n", (double)v, (double)a);
}

static String handle(const String &raw) {
  String cmd = raw;
  cmd.trim();
  cmd.toUpperCase();
  s_last_cmd = cmd;
  Serial.printf("> %s\n", cmd.c_str());

  if (cmd == "ATZ") return "ELM327 v1.5";
  if (cmd == "ATI") return "OBDSIM 0.1";
  if (cmd == "ATE0" || cmd == "ATL0" || cmd == "ATS0" || cmd == "ATH1") return "OK";
  if (cmd.startsWith("ATSP") || cmd.startsWith("ATAT") || cmd == "ATDP") return "OK";
  if (cmd == "0100") return "41 00 BE 1F B8 10";
  if (cmd == "010C") {
    int rpm = 850 + (int)(100 * sinf(millis() / 5000.0f));
    uint16_t v = (uint16_t)(rpm * 4);
    char b[32];
    snprintf(b, sizeof(b), "41 0C %02X %02X", v >> 8, v & 0xFF);
    return b;
  }
  if (cmd == "010D") {
    // Cruise 100 km/h except in the park window so the aero tip demos.
    bool park = (millis() / 1000 % 60) >= 45;
    return park ? "41 0D 00" : "41 0D 64";
  }
  if (cmd == "0142") {
    uint16_t mv = 14200;
    char b[32];
    snprintf(b, sizeof(b), "41 42 %02X %02X", mv >> 8, mv & 0xFF);
    return b;
  }
  if (cmd == "015B") return "41 5B B8";
  if (cmd == "22480A") {
    // Desk stand-in current: -30 A charging in the park window, +20 A out.
    bool park = (millis() / 1000 % 60) >= 45;
    uint16_t u = (uint16_t)(park ? -3000 : 2000);
    char b[32];
    snprintf(b, sizeof(b), "62 48 0A %02X %02X", (u >> 8) & 0xFF, u & 0xFF);
    return b;
  }
  if (cmd == "22484E") {
    // Desk stand-in charge power: 11500 W in the park window, else 0.
    bool park = (millis() / 1000 % 60) >= 45;
    uint16_t w = park ? 2300 : 0;
    char b[32];
    snprintf(b, sizeof(b), "62 48 4E %02X %02X", (w >> 8) & 0xFF, w & 0xFF);
    return b;
  }
  if (cmd == "224845") return "62 48 45 97";
  if (cmd == "22485E") return "62 48 5E 5D C0";
  if (cmd == "226101") {
    float v, a;
    packScript(v, a);
    uint32_t mv = (uint32_t)(v * 1000);
    char b[32];
    snprintf(b, sizeof(b), "62 61 01 %02X %02X %02X", (mv >> 16) & 0xFF, (mv >> 8) & 0xFF,
             mv & 0xFF);
    return b;
  }
  if (cmd == "226103") {
    // Scripted shifter: PARK 15 s per minute so charge mode demos.
    bool park = (millis() / 1000 % 60) >= 45;
    char b[32];
    snprintf(b, sizeof(b), "62 61 03 %02X", park ? 0x50 : 0x44);
    return b;
  }
  if (cmd == "226102") {
    float v, a;
    packScript(v, a);
    int32_t ma = (int32_t)(a * 1000);
    uint32_t u = (uint32_t)(ma & 0xFFFFFF);
    char b[32];
    snprintf(b, sizeof(b), "62 61 02 %02X %02X %02X", (u >> 16) & 0xFF, (u >> 8) & 0xFF,
             u & 0xFF);
    return b;
  }
  return "?";
}

class ChrCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    std::string v = c->getValue();
    String cmd(v.c_str());
    cmd.trim();
    if (cmd.length() == 0) return;
    answer(handle(cmd));
  }
  void onSubscribe(NimBLECharacteristic *c, ble_gap_conn_desc *desc, uint16_t sub) override {
    (void)c;
    s_conn = desc->conn_handle;
    Serial.printf("[sim] subscribe conn=%d sub=%d\n", s_conn, sub);
  }
};

class SrvCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s, ble_gap_conn_desc *desc) override {
    (void)s;
    s_conn = desc->conn_handle;
    Serial.printf("[sim] connect handle=%d\n", s_conn);
  }
  void onDisconnect(NimBLEServer *s, ble_gap_conn_desc *desc) override {
    (void)s;
    (void)desc;
    s_conn = BLE_HS_CONN_HANDLE_NONE;
    Serial.println("[sim] disconnect");
    NimBLEDevice::startAdvertising();
  }
};

void setup(void) {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("[sim] OBDSIM 0.1 starting");

  s_tft.init();
  s_tft.setRotation(1);
  s_tft.setBrightness(128);
  screen_draw();

  NimBLEDevice::init("OBDSIM");
  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(new SrvCallbacks());
  NimBLEService *svc = s_server->createService(SIM_SVC);
  s_chr = svc->createCharacteristic(SIM_CHR, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE |
                                                 NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
  s_chr->setCallbacks(new ChrCallbacks());
  svc->start();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SIM_SVC);
  adv->setName("OBDSIM");
  adv->setScanResponse(true);
  NimBLEDevice::startAdvertising();
  Serial.println("[sim] advertising OBDSIM / FFE0");
}

void loop(void) {
  // Backlight breathing: visible on ANY panel variant, proves the
  // firmware runs even when the panel driver mismatches (white screen).
  s_tft.setBrightness(30 + (uint8_t)(90 * (0.5f + 0.5f * sinf(millis() / 2000.0f))));
  static uint32_t last = 0;
  if (millis() - last > 1000) {
    last = millis();
    screen_draw();
  }
  static uint32_t last_log = 0;
  if (millis() - last_log > 5000) {
    last_log = millis();
    float v, a;
    packScript(v, a);
    Serial.printf("[sim] pack %.1f V %.1f A conn=%s\n", v, a,
                  s_conn == BLE_HS_CONN_HANDLE_NONE ? "no" : "yes");
  }
  delay(50);
}
