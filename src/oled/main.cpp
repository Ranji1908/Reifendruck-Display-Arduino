// Reifendruck-Display (TPMS) fuer Transporter wie den Sprinter W906
// ESP32 + BLE-Ventilkappen-Sensoren + 128x64 OLED + Drehgeber + Summer
//
// Bedienung (Drehgeber mit Taster):
//   Hauptanzeige: kurz druecken  -> Strasse/Offroad umschalten (bei Alarm: quittieren)
//                 drehen         -> Druck- / Temperaturansicht
//                 lang druecken  -> Menue
//   Menue:        drehen = waehlen, kurz = bearbeiten/OK, lang = zurueck

#include <Arduino.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "config.h"  // vor BleTpms.h (DEBUG_BLE)
#include "BleTpms.h"
#include "TpmsCore.h"

using namespace tpms;

// ============================================================ Display
#if DISPLAY_FLIP
#define DISP_ROT U8G2_R2
#else
#define DISP_ROT U8G2_R0
#endif
#if DISPLAY_SH1106
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(DISP_ROT, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(DISP_ROT, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);
#endif

// ============================================================ Zustand
static Settings settings;
static uint64_t sensorIds[WHEEL_COUNT] = {0, 0, 0, 0};
static WheelMonitor monitors[WHEEL_COUNT];
static State states[WHEEL_COUNT] = {State::NotPaired, State::NotPaired, State::NotPaired, State::NotPaired};
static AlarmManager alarms;
static Preferences prefs;

enum class Screen : uint8_t { Main, Menu, Edit, Pair };
static Screen screen = Screen::Main;
static bool showTemps = false;
static uint32_t overlayUntil = 0;
static uint32_t lastInputMs = 0;

// ============================================================ Speicher
static const uint8_t SETTINGS_VERSION = 2;

static void saveSettings() {
  prefs.putUChar("ver", SETTINGS_VERSION);
  prefs.putBytes("cfg", &settings, sizeof(settings));
}

static void saveSensors() { prefs.putBytes("ids", sensorIds, sizeof(sensorIds)); }

static void loadAll() {
  prefs.begin("tpms", false);
  if (prefs.getUChar("ver", 0) == SETTINGS_VERSION && prefs.getBytesLength("cfg") == sizeof(settings)) {
    prefs.getBytes("cfg", &settings, sizeof(settings));
  }
  clampSettings(settings);
  if (prefs.getBytesLength("ids") == sizeof(sensorIds)) prefs.getBytes("ids", sensorIds, sizeof(sensorIds));
}

static Mode currentMode() { return (Mode)settings.mode; }
static bool isPaired(int w) { return settings.demo || sensorIds[w] != 0; }

// ============================================================ Summer
struct Beeper {
  const uint16_t* pattern = nullptr;  // an, aus, an, aus ... (ms)
  uint8_t len = 0, idx = 0;
  bool repeat = false;
  uint32_t stepStart = 0;

  void play(const uint16_t* p, uint8_t l, bool rep) {
    if (pattern == p && repeat == rep) return;  // laeuft schon
    pattern = p;
    len = l;
    repeat = rep;
    idx = 0;
    stepStart = millis();
    out(true);
  }
  void stop() {
    pattern = nullptr;
    out(false);
  }
  bool busy() const { return pattern != nullptr; }
  void update() {
    if (!pattern) return;
    if (millis() - stepStart < pattern[idx]) return;
    stepStart = millis();
    if (++idx >= len) {
      if (!repeat) return stop();
      idx = 0;
    }
    out((idx % 2) == 0);
  }
  void out(bool on) { digitalWrite(PIN_BUZZER, (on && settings.buzzer) ? HIGH : LOW); }
};
static Beeper beeper;
static const uint16_t BEEP_WARN[] = {120, 120, 120, 10};
static const uint16_t BEEP_ALARM[] = {250, 120, 250, 120, 250, 1500};
static const uint16_t BEEP_CLICK[] = {25, 5};
static const uint16_t BEEP_OK[] = {60, 60, 160, 5};

static void click() {
  if (!beeper.repeat) beeper.play(BEEP_CLICK, 2, false);
}

// ============================================================ Eingabe
static volatile int32_t encRaw = 0;
static volatile uint8_t encState = 0;

static void IRAM_ATTR encIsr() {
  static const int8_t table[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
  uint8_t s = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  encState = ((encState << 2) | s) & 0x0F;
  encRaw += table[encState];
}

static int readEncoder() {
  static int32_t consumed = 0;
  int32_t raw = encRaw;
  int steps = (raw - consumed) / ENC_STEPS_PER_DETENT;
  consumed += steps * ENC_STEPS_PER_DETENT;
  return ENC_REVERSE ? -steps : steps;
}

enum class Btn : uint8_t { None, Short, Long };

static Btn readButton() {
  static bool stable = HIGH, lastRead = HIGH, longFired = false;
  static uint32_t changeMs = 0, pressMs = 0;
  bool r = digitalRead(PIN_ENC_BTN);
  uint32_t now = millis();
  if (r != lastRead) {
    lastRead = r;
    changeMs = now;
  }
  if (now - changeMs > 25 && r != stable) {
    stable = r;
    if (stable == LOW) {
      pressMs = now;
      longFired = false;
    } else if (!longFired) {
      return Btn::Short;
    }
  }
  if (stable == LOW && !longFired && now - pressMs > LONG_PRESS_MS) {
    longFired = true;
    return Btn::Long;
  }
  return Btn::None;
}

// ============================================================ Menue
enum MenuItem : uint8_t {
  M_ROAD_FRONT,
  M_ROAD_REAR,
  M_OFF_FRONT,
  M_OFF_REAR,
  M_WARN,
  M_CRIT,
  M_HIGH,
  M_LEAK_BAR,
  M_LEAK_MIN,
  M_PAIR,
  M_BRIGHT,
  M_SOUND,
  M_DEMO,
  M_BACK,
  M_COUNT
};
static const char* const MENU_LABELS[M_COUNT] = {
    "Soll Str. vorn", "Soll Str. hinten", "Soll Off. vorn", "Soll Off. hinten", "Warnung ab -%",
    "Kritisch ab -%", "Hinweis ab +%",   "Leck: Verlust",  "Leck: in Min.",    "Sensoren anlernen",
    "Helligkeit",     "Warnton",         "Demo-Modus",     "< Zurueck"};
static int menuIdx = 0, menuTop = 0;

static float* targetPtr(int item) {
  switch (item) {
    case M_ROAD_FRONT: return &settings.target[ROAD][0];
    case M_ROAD_REAR: return &settings.target[ROAD][1];
    case M_OFF_FRONT: return &settings.target[OFFROAD][0];
    case M_OFF_REAR: return &settings.target[OFFROAD][1];
    default: return nullptr;
  }
}

static void menuValueText(int item, char* buf, size_t n) {
  if (float* t = targetPtr(item)) {
    snprintf(buf, n, "%.1f bar", *t);
    return;
  }
  switch (item) {
    case M_WARN: snprintf(buf, n, "%u %%", (unsigned)settings.warnPct); break;
    case M_CRIT: snprintf(buf, n, "%u %%", (unsigned)settings.critPct); break;
    case M_HIGH: snprintf(buf, n, "%u %%", (unsigned)settings.highPct); break;
    case M_LEAK_BAR: snprintf(buf, n, "%.1f bar", settings.leakDeciBar / 10.0f); break;
    case M_LEAK_MIN: snprintf(buf, n, "%u min", (unsigned)settings.leakMinutes); break;
    case M_BRIGHT: snprintf(buf, n, "%u", (unsigned)settings.contrast); break;
    case M_SOUND: snprintf(buf, n, "%s", settings.buzzer ? "an" : "aus"); break;
    case M_DEMO: snprintf(buf, n, "%s", settings.demo ? "an" : "aus"); break;
    default: buf[0] = 0;
  }
}

static int addClamped(int v, int d, int lo, int hi) { return v + d < lo ? lo : (v + d > hi ? hi : v + d); }

static void menuAdjust(int item, int d) {
  if (float* t = targetPtr(item)) {
    int deci = (int)lroundf(*t * 10.0f);
    *t = addClamped(deci, d, 5, 80) / 10.0f;
    return;
  }
  switch (item) {
    case M_WARN: settings.warnPct = addClamped(settings.warnPct, d, 3, 40); break;
    case M_CRIT: settings.critPct = addClamped(settings.critPct, d, 5, 60); break;
    case M_HIGH: settings.highPct = addClamped(settings.highPct, d, 5, 60); break;
    case M_LEAK_BAR: settings.leakDeciBar = addClamped(settings.leakDeciBar, d, 1, 20); break;
    case M_LEAK_MIN: settings.leakMinutes = addClamped(settings.leakMinutes, d, 1, 15); break;
    case M_BRIGHT:
      settings.contrast = addClamped(settings.contrast, d * 16, 0, 255);
      u8g2.setContrast((unsigned)settings.contrast);
      break;
    case M_SOUND: settings.buzzer = !settings.buzzer; break;
    case M_DEMO: settings.demo = !settings.demo; break;
  }
}

// ============================================================ Anlernen
static int pairWheel = 0;
static bool pairHasCandidate = false;
static SensorReading pairCandidate;

static void startPairing() {
  pairWheel = 0;
  pairHasCandidate = false;
  screen = Screen::Pair;
}

static bool idUsedByOtherWheel(uint64_t id, int wheel) {
  for (int w = 0; w < WHEEL_COUNT; ++w)
    if (w != wheel && sensorIds[w] == id) return true;
  return false;
}

static void pairingOnReading(const SensorReading& r) {
  if (idUsedByOtherWheel(r.id, pairWheel)) return;
  // bei mehreren Kandidaten den naechstgelegenen (staerkstes Signal) nehmen
  if (!pairHasCandidate || r.id == pairCandidate.id || r.rssi > pairCandidate.rssi + 6) {
    pairCandidate = r;
    pairHasCandidate = true;
  }
}

static void pairingNext(bool accept) {
  if (accept && pairHasCandidate) {
    sensorIds[pairWheel] = pairCandidate.id;
    monitors[pairWheel].reset();
    monitors[pairWheel].addReading(pairCandidate, millis());
    beeper.play(BEEP_OK, 4, false);
  }
  pairHasCandidate = false;
  if (++pairWheel >= WHEEL_COUNT) {
    saveSensors();
    screen = Screen::Main;
  }
}

// ============================================================ Demo-Modus
static void demoTick(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 3000) return;
  last = now;
  float t = now / 1000.0f;
  for (int w = 0; w < WHEEL_COUNT; ++w) {
    SensorReading r;
    r.id = w + 1;
    float target = targetFor(settings, currentMode(), (Wheel)w);
    r.pressureBar = target + 0.05f * sinf(t / 20.0f + w);
    // HL verliert in einem 6-Minuten-Zyklus Luft -> Warnung, dann Leck-Alarm
    if (w == RL) r.pressureBar = target - fmodf(t, 360.0f) * 0.0025f;
    r.temperatureC = 18.0f + w + 2.0f * sinf(t / 60.0f);
    r.batteryPct = w == RR ? 15 : 85;
    monitors[w].addReading(r, now);
  }
}

// ============================================================ Zeichnen
static bool blinkOn() { return (millis() / 400) % 2 == 0; }

static void drawVan() {
  // Draufsicht, Fahrtrichtung nach oben
  u8g2.drawRFrame(54, 13, 20, 50, 4);  // Karosserie
  u8g2.drawLine(56, 23, 71, 23);       // Windschutzscheibe
  u8g2.drawLine(56, 30, 71, 30);       // Kabine
  u8g2.drawLine(64, 14, 64, 16);       // Stern-Andeutung ;-)
  const int wx[4] = {51, 74, 51, 74}, wy[4] = {17, 17, 49, 49};
  for (int w = 0; w < WHEEL_COUNT; ++w) {
    if (severity(states[w]) >= 2 && blinkOn())
      u8g2.drawBox(wx[w] - 1, wy[w] - 1, 5, 12);
    else
      u8g2.drawBox(wx[w], wy[w], 3, 10);
  }
}

static void drawWheelBlock(int w) {
  const bool left = (w == FL || w == RL);
  const bool top = w < RL;
  const int baseY = top ? 29 : 54;   // Grundlinie der grossen Zahl
  const int boxY = top ? 12 : 38;
  const State s = states[w];
  const uint8_t sev = severity(s);
  const int x0 = left ? 0 : 80;
  const int bw = 48;

  // Warnung/Alarm: Block invertiert blinkend
  bool invert = sev >= 2 && blinkOn();
  if (invert) {
    u8g2.drawBox(x0, boxY, bw, 26);
    u8g2.setDrawColor(0);
  }

  char big[8];
  if (s == State::NotPaired || s == State::NoData || !monitors[w].hasData())
    strcpy(big, "--");
  else if (showTemps)
    snprintf(big, sizeof(big), "%d", (int)lroundf(monitors[w].temperature()));
  else
    snprintf(big, sizeof(big), "%.1f", monitors[w].pressure());

  u8g2.setFont(u8g2_font_logisoso16_tn);
  int tw = u8g2.getStrWidth(big);
  int x = left ? (x0 + bw - 3 - tw) : (x0 + 3);
  u8g2.drawStr(x, baseY, big);

  // kleine Zeile: Status, sonst Soll bzw. Temperatur
  char small[16];
  if (sev > 0 || s == State::NotPaired || s == State::NoData)
    snprintf(small, sizeof(small), "%s", stateText(s));
  else if (monitors[w].battery() < 20)
    snprintf(small, sizeof(small), "Batt %u%%", (unsigned)monitors[w].battery());
  else if (showTemps)
    snprintf(small, sizeof(small), "Grad C");
  else
    snprintf(small, sizeof(small), "Soll %.1f", targetFor(settings, currentMode(), (Wheel)w));
  u8g2.setFont(u8g2_font_5x7_tf);
  tw = u8g2.getStrWidth(small);
  x = left ? (x0 + bw - 3 - tw) : (x0 + 3);
  u8g2.drawStr(x, baseY + 8, small);
  u8g2.setDrawColor(1);
}

static void drawMain() {
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 8, modeName(currentMode()));
  Wheel worst = alarms.worstWheel(states);
  if (worst != WHEEL_COUNT && severity(states[worst]) >= 2) {
    char buf[16];
    snprintf(buf, sizeof(buf), "! %s %s", wheelName(worst), stateText(states[worst]));
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(128 - u8g2.getStrWidth(buf), 7, buf);
  } else if (settings.demo) {
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(103, 7, "DEMO");
  } else if (showTemps) {
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(93, 7, "Temp.");
  }
  drawVan();
  for (int w = 0; w < WHEEL_COUNT; ++w) drawWheelBlock(w);

  if (millis() < overlayUntil) {  // Einblendung nach Moduswechsel
    u8g2.setDrawColor(0);
    u8g2.drawBox(8, 18, 112, 30);
    u8g2.setDrawColor(1);
    u8g2.drawRFrame(8, 18, 112, 30, 4);
    u8g2.setFont(u8g2_font_helvB12_tf);
    const char* m = modeName(currentMode());
    u8g2.drawStr(64 - u8g2.getStrWidth(m) / 2, 39, m);
  }
}

static void drawMenu(bool editing) {
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 8, editing ? "Wert aendern" : "Einstellungen");
  u8g2.drawHLine(0, 10, 128);
  const int rows = 4;
  if (menuIdx < menuTop) menuTop = menuIdx;
  if (menuIdx >= menuTop + rows) menuTop = menuIdx - rows + 1;
  for (int r = 0; r < rows && menuTop + r < M_COUNT; ++r) {
    int item = menuTop + r;
    int y = 22 + r * 12;
    bool sel = item == menuIdx;
    if (sel) {
      u8g2.drawBox(0, y - 9, 128, 12);
      u8g2.setDrawColor(0);
    }
    u8g2.drawStr(2, y, MENU_LABELS[item]);
    char val[16];
    menuValueText(item, val, sizeof(val));
    if (editing && sel && blinkOn()) val[0] = 0;
    u8g2.drawStr(126 - u8g2.getStrWidth(val), y, val);
    u8g2.setDrawColor(1);
  }
}

static void drawPair() {
  u8g2.setFont(u8g2_font_6x10_tf);
  char buf[32];
  snprintf(buf, sizeof(buf), "Anlernen %d/4: %s", pairWheel + 1, wheelName((Wheel)pairWheel));
  u8g2.drawStr(0, 8, buf);
  u8g2.drawHLine(0, 10, 128);
  if (pairHasCandidate) {
    char id[12];
    formatShortId(pairCandidate.id, id, sizeof(id));
    snprintf(buf, sizeof(buf), "Sensor %s", id);
    u8g2.drawStr(0, 24, buf);
    snprintf(buf, sizeof(buf), "%.2f bar  %ddBm", pairCandidate.pressureBar, pairCandidate.rssi);
    u8g2.drawStr(0, 36, buf);
    u8g2.drawStr(0, 52, "Kurz: uebernehmen");
  } else {
    u8g2.drawStr(0, 24, "Sensor jetzt auf");
    u8g2.drawStr(0, 36, "dieses Rad schrauben");
    if (blinkOn()) u8g2.drawStr(0, 52, "suche ...");
  }
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 63, pairHasCandidate ? "Lang: Abbruch" : "Kurz: ueberspr. Lang: Abbr.");
}

// ============================================================ Ablauf
static void handleInput() {
  int steps = readEncoder();
  Btn b = readButton();
  uint32_t now = millis();
  if (steps != 0 || b != Btn::None) lastInputMs = now;

  switch (screen) {
    case Screen::Main:
      if (steps != 0) showTemps = !showTemps;
      if (b == Btn::Short) {
        if (alarms.hasUnacknowledged()) {
          alarms.acknowledgeAll();
          beeper.stop();
        } else {
          settings.mode = currentMode() == ROAD ? OFFROAD : ROAD;
          saveSettings();
          overlayUntil = now + OVERLAY_MS;
          click();
        }
      } else if (b == Btn::Long) {
        screen = Screen::Menu;
        menuIdx = 0;
        click();
      }
      break;

    case Screen::Menu:
      if (steps != 0) menuIdx = addClamped(menuIdx, steps, 0, M_COUNT - 1);
      if (b == Btn::Short) {
        click();
        if (menuIdx == M_BACK) screen = Screen::Main;
        else if (menuIdx == M_PAIR) startPairing();
        else if (menuIdx == M_SOUND || menuIdx == M_DEMO) {
          menuAdjust(menuIdx, 1);
          if (menuIdx == M_DEMO)
            for (auto& m : monitors) m.reset();  // Demo an/aus -> alte Werte verwerfen
          saveSettings();
        } else screen = Screen::Edit;
      } else if (b == Btn::Long || now - lastInputMs > MENU_TIMEOUT_MS) {
        screen = Screen::Main;
      }
      break;

    case Screen::Edit:
      if (steps != 0) menuAdjust(menuIdx, steps);
      if (b != Btn::None || now - lastInputMs > MENU_TIMEOUT_MS) {
        clampSettings(settings);
        saveSettings();
        click();
        screen = Screen::Menu;
      }
      break;

    case Screen::Pair:
      if (b == Btn::Short) pairingNext(true);
      else if (b == Btn::Long) {
        saveSensors();
        screen = Screen::Main;
      }
      break;
  }
}

static void processReadings(uint32_t now) {
  SensorReading r;
  while (blet::poll(r)) {
    if (screen == Screen::Pair) pairingOnReading(r);
    if (settings.demo) continue;
    for (int w = 0; w < WHEEL_COUNT; ++w)
      if (sensorIds[w] != 0 && sensorIds[w] == r.id) monitors[w].addReading(r, now);
  }
}

static void evaluateAll(uint32_t now) {
  EvalParams p = paramsFrom(settings);
  for (int w = 0; w < WHEEL_COUNT; ++w)
    states[w] = monitors[w].evaluate(targetFor(settings, currentMode(), (Wheel)w), p, now, isPaired(w));

  switch (alarms.update(states)) {
    case AlarmManager::Sound::Alarm: beeper.play(BEEP_ALARM, 6, true); break;
    case AlarmManager::Sound::WarnOnce: beeper.play(BEEP_WARN, 4, false); break;
    case AlarmManager::Sound::None:
      if (beeper.repeat && !alarms.hasUnacknowledged()) beeper.stop();
      break;
  }
}

static void logStatus(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 5000) return;
  last = now;
  Serial.printf("[%s]", modeName(currentMode()));
  for (int w = 0; w < WHEEL_COUNT; ++w)
    Serial.printf("  %s %.2fbar %s", wheelName((Wheel)w), monitors[w].pressure(), stateText(states[w]));
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_BTN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encIsr, CHANGE);

  loadAll();

  u8g2.setBusClock(400000);
  u8g2.begin();
  u8g2.setContrast((unsigned)settings.contrast);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB12_tf);
  u8g2.drawStr(14, 30, "Reifendruck");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(30, 48, modeName(currentMode()));
  u8g2.sendBuffer();

  blet::begin();
  overlayUntil = millis() + 800;
  Serial.println("Reifendruck-Display gestartet");
}

void loop() {
  uint32_t now = millis();
  handleInput();
  processReadings(now);
  if (settings.demo) demoTick(now);

  static uint32_t lastEval = 0, lastDraw = 0, lastScanCheck = 0;
  if (now - lastEval >= 500) {
    lastEval = now;
    evaluateAll(now);
  }
  if (now - lastScanCheck >= 10000) {
    lastScanCheck = now;
    blet::keepAlive();
    logStatus(now);
  }
  beeper.update();

  if (now - lastDraw >= 100) {  // ~10 fps, Drehgeber laeuft per Interrupt
    lastDraw = now;
    u8g2.clearBuffer();
    switch (screen) {
      case Screen::Main: drawMain(); break;
      case Screen::Menu: drawMenu(false); break;
      case Screen::Edit: drawMenu(true); break;
      case Screen::Pair: drawPair(); break;
    }
    u8g2.sendBuffer();
  }
}
