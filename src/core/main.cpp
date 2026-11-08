// Reifendruck- und Neigungsanzeige fuer den Sprinter W906 – Variante M5Stack Core2 / CoreS3
//
// Hardware: M5Stack Core2 (empfohlen) oder CoreS3: 2"-Touchdisplay 320x240 im fertigen Gehaeuse,
//           Lautsprecher und Lagesensor (Neigung) sind eingebaut.
//           + 4 BLE-Ventilkappen-Sensoren
//
// Bedienung per Touch (beim Core2 gehen auch die drei Tasten-Punkte unter dem Display):
//   Hauptanzeige:  links tippen  = vorige Seite      rechts tippen = naechste Seite
//                  Mitte tippen  = Strasse <-> Offroad (bei Alarm: quittieren)
//                  lange halten  = Menue              wischen       = Seite wechseln
//   Menue:         Eintrag antippen = aendern, Leiste unten: hoch / zurueck / runter
//   Wert aendern:  Leiste unten: - / OK / +

#include <M5Unified.h>
#include <Preferences.h>

#include "BleTpms.h"
#include "TpmsCore.h"

using namespace tpms;

#define MENU_TIMEOUT_MS 30000
#define OVERLAY_MS 1500
#define W 320
#define H 240
#define BAR_Y 196  // Ab hier: Bedienleiste in Menue/Editor

// ============================================================ Farben
static const uint16_t C_BG = TFT_BLACK;
static const uint16_t C_TEXT = TFT_WHITE;
static const uint16_t C_DIM = 0x7BEF;   // grau
static const uint16_t C_DARK = 0x2945;  // dunkelgrau
static const uint16_t C_OK = 0x07E8;    // gruen
static const uint16_t C_WARN = 0xFD20;  // orange
static const uint16_t C_CRIT = 0xF800;  // rot
static const uint16_t C_ROAD = 0x5D7F;  // hellblau
static const uint16_t C_OFF = 0xFE60;   // gelb-orange

static M5Canvas canvas(&M5.Display);

// ============================================================ Zustand
static Settings settings;
static uint64_t sensorIds[WHEEL_COUNT] = {0, 0, 0, 0};
static WheelMonitor monitors[WHEEL_COUNT];
static State states[WHEEL_COUNT] = {State::NotPaired, State::NotPaired, State::NotPaired, State::NotPaired};
static AlarmManager alarms;
static Preferences prefs;

static TiltCalc tilt;
static bool imuOk = false;
static Vec3 accelFilt = {0, 0, 1};
static float rollDeg = 0, pitchDeg = 0;
static bool tiltWarn = false;

enum class Screen : uint8_t { Main, Menu, Edit, Pair, Calib };
enum Page : uint8_t { P_TIRES, P_TILT, P_LEVEL, PAGE_COUNT };
static Screen screen = Screen::Main;
static int page = P_TIRES;
static uint32_t overlayUntil = 0;
static const char* overlayText = nullptr;
static uint32_t lastInputMs = 0;

static Mode currentMode() { return (Mode)settings.mode; }
static bool isPaired(int w) { return settings.demo || sensorIds[w] != 0; }

static void showOverlay(const char* text) {
  overlayText = text;
  overlayUntil = millis() + OVERLAY_MS;
}

// ============================================================ Speicher
static const uint8_t SETTINGS_VERSION = 2;

static void applySettings() {
  M5.Display.setBrightness(settings.contrast);
  M5.Speaker.setVolume(settings.volume);
  tilt.setup({settings.level[0], settings.level[1], settings.level[2]}, settings.mountQuarter);
}

static void saveSettings() {
  prefs.putUChar("ver", SETTINGS_VERSION);
  prefs.putBytes("cfg", &settings, sizeof(settings));
}

static void saveSensors() { prefs.putBytes("ids", sensorIds, sizeof(sensorIds)); }

static void loadAll() {
  prefs.begin("tpms", false);
  if (prefs.getUChar("ver", 0) == SETTINGS_VERSION && prefs.getBytesLength("cfg") == sizeof(settings))
    prefs.getBytes("cfg", &settings, sizeof(settings));
  clampSettings(settings);
  if (prefs.getBytesLength("ids") == sizeof(sensorIds)) prefs.getBytes("ids", sensorIds, sizeof(sensorIds));
}

// ============================================================ Ton
struct Beeper {
  const uint16_t* pattern = nullptr;  // an, aus, an, aus ... (ms)
  uint8_t len = 0, idx = 0;
  bool repeat = false;
  uint16_t freq = 3000;
  uint32_t stepStart = 0;

  void play(const uint16_t* p, uint8_t l, bool rep, uint16_t f) {
    if (pattern == p && repeat == rep) return;
    pattern = p;
    len = l;
    repeat = rep;
    freq = f;
    idx = 0;
    stepStart = millis();
    on();
  }
  void stop() {
    pattern = nullptr;
    repeat = false;
    M5.Speaker.stop();
  }
  void on() {
    if (settings.buzzer) M5.Speaker.tone(freq, pattern[idx]);
  }
  void update() {
    if (!pattern || millis() - stepStart < pattern[idx]) return;
    stepStart = millis();
    if (++idx >= len) {
      if (!repeat) {
        pattern = nullptr;
        return;
      }
      idx = 0;
    }
    if (idx % 2 == 0) on();
  }
};
static Beeper beeper;
static const uint16_t BEEP_WARN[] = {120, 120, 120, 10};
static const uint16_t BEEP_ALARM[] = {250, 120, 250, 120, 250, 1500};
static const uint16_t BEEP_TILT[] = {80, 60, 80, 60, 80, 10};
static const uint16_t BEEP_OK[] = {60, 60, 160, 5};

static void click() {
  if (!beeper.repeat && settings.buzzer) M5.Speaker.tone(5000, 12);
}

// ============================================================ Menue-Daten
enum MenuItem : uint8_t {
  M_ROAD_F, M_ROAD_R, M_OFF_F, M_OFF_R, M_WARN, M_CRIT, M_HIGH, M_LEAK_BAR, M_LEAK_MIN, M_PAIR,
  M_CALIB, M_MOUNT, M_TILT_R, M_TILT_P, M_WB, M_TRACK, M_BRIGHT, M_VOL, M_SOUND, M_DEMO, M_BACK,
  M_COUNT
};
static const char* const MENU_LABELS[M_COUNT] = {
    "Soll Strasse vorn",  "Soll Strasse hinten", "Soll Offroad vorn", "Soll Offroad hinten",
    "Warnung ab",         "Kritisch ab",         "Zu hoch ab",        "Leck: Verlust",
    "Leck: Zeitraum",     "Sensoren anlernen",   "Neigung nullen",    "Einbaulage Sensor",
    "Warnung Seite",      "Warnung Laengs",      "Radstand",          "Spurweite",
    "Helligkeit",         "Lautstaerke",         "Warnton",           "Demo-Modus",
    "< Zurueck"};
static int menuIdx = 0, menuTop = 0;
static const int MENU_ROWS = 4, ROW_H = 40, ROW_Y0 = 32;

static float* targetPtr(int item) {
  switch (item) {
    case M_ROAD_F: return &settings.target[ROAD][0];
    case M_ROAD_R: return &settings.target[ROAD][1];
    case M_OFF_F: return &settings.target[OFFROAD][0];
    case M_OFF_R: return &settings.target[OFFROAD][1];
    default: return nullptr;
  }
}

static void menuValueText(int item, char* buf, size_t n) {
  if (float* t = targetPtr(item)) {
    snprintf(buf, n, "%.1f bar", *t);
    return;
  }
  switch (item) {
    case M_WARN: snprintf(buf, n, "-%u %%", (unsigned)settings.warnPct); break;
    case M_CRIT: snprintf(buf, n, "-%u %%", (unsigned)settings.critPct); break;
    case M_HIGH: snprintf(buf, n, "+%u %%", (unsigned)settings.highPct); break;
    case M_LEAK_BAR: snprintf(buf, n, "%.1f bar", settings.leakDeciBar / 10.0f); break;
    case M_LEAK_MIN: snprintf(buf, n, "%u min", (unsigned)settings.leakMinutes); break;
    case M_MOUNT: snprintf(buf, n, "%u Grad", (unsigned)settings.mountQuarter * 90); break;
    case M_TILT_R: snprintf(buf, n, "%u Grad", (unsigned)settings.tiltWarnRoll); break;
    case M_TILT_P: snprintf(buf, n, "%u Grad", (unsigned)settings.tiltWarnPitch); break;
    case M_WB: snprintf(buf, n, "%u cm", (unsigned)settings.wheelbaseCm); break;
    case M_TRACK: snprintf(buf, n, "%u cm", (unsigned)settings.trackCm); break;
    case M_BRIGHT: snprintf(buf, n, "%u %%", (unsigned)(settings.contrast * 100 / 255)); break;
    case M_VOL: snprintf(buf, n, "%u %%", (unsigned)(settings.volume * 100 / 255)); break;
    case M_SOUND: snprintf(buf, n, "%s", settings.buzzer ? "an" : "aus"); break;
    case M_DEMO: snprintf(buf, n, "%s", settings.demo ? "an" : "aus"); break;
    default: buf[0] = 0;
  }
}

static int addClamped(int v, int d, int lo, int hi) { return v + d < lo ? lo : (v + d > hi ? hi : v + d); }

static void menuAdjust(int item, int d) {
  if (float* t = targetPtr(item)) {
    *t = addClamped((int)lroundf(*t * 10.0f), d, 5, 80) / 10.0f;
    return;
  }
  switch (item) {
    case M_WARN: settings.warnPct = addClamped(settings.warnPct, d, 3, 40); break;
    case M_CRIT: settings.critPct = addClamped(settings.critPct, d, 5, 60); break;
    case M_HIGH: settings.highPct = addClamped(settings.highPct, d, 5, 60); break;
    case M_LEAK_BAR: settings.leakDeciBar = addClamped(settings.leakDeciBar, d, 1, 20); break;
    case M_LEAK_MIN: settings.leakMinutes = addClamped(settings.leakMinutes, d, 1, 15); break;
    case M_MOUNT: settings.mountQuarter = (uint8_t)((settings.mountQuarter + d + 400) % 4); break;
    case M_TILT_R: settings.tiltWarnRoll = addClamped(settings.tiltWarnRoll, d, 5, 45); break;
    case M_TILT_P: settings.tiltWarnPitch = addClamped(settings.tiltWarnPitch, d, 5, 45); break;
    case M_WB: settings.wheelbaseCm = addClamped(settings.wheelbaseCm, d, 150, 600); break;
    case M_TRACK: settings.trackCm = addClamped(settings.trackCm, d, 100, 250); break;
    case M_BRIGHT: settings.contrast = addClamped(settings.contrast, d * 13, 10, 255); break;
    case M_VOL: settings.volume = addClamped(settings.volume, d * 13, 0, 255); break;
  }
  applySettings();
}

// ============================================================ Anlernen
static int pairWheel = 0;
static bool pairHasCandidate = false;
static SensorReading pairCandidate;

static bool idUsedByOtherWheel(uint64_t id, int wheel) {
  for (int w = 0; w < WHEEL_COUNT; ++w)
    if (w != wheel && sensorIds[w] == id) return true;
  return false;
}

static void pairingOnReading(const SensorReading& r) {
  if (idUsedByOtherWheel(r.id, pairWheel)) return;
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
    beeper.play(BEEP_OK, 4, false, 4000);
  }
  pairHasCandidate = false;
  if (++pairWheel >= WHEEL_COUNT) {
    saveSensors();
    screen = Screen::Main;
    page = P_TIRES;
  }
}

// ============================================================ Neigung
static void updateTilt() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 20) return;  // 50 Hz
  last = now;

  Vec3 a;
  if (settings.demo) {
    float t = now / 1000.0f;
    float r = 6.0f * sinf(t / 7.0f) * 0.0174533f, p = 4.0f * sinf(t / 11.0f) * 0.0174533f;
    a = {sinf(p), sinf(r) * cosf(p), cosf(r) * cosf(p)};
    TiltCalc flat;
    flat.setup({0, 0, 1}, 0);
    flat.compute(a, rollDeg, pitchDeg);
    return;
  }
  if (!imuOk) return;
  M5.Imu.update();
  if (!M5.Imu.getAccel(&a.x, &a.y, &a.z)) return;
  const float k = 0.02f;  // Tiefpass, Zeitkonstante ca. 1 s
  accelFilt.x += k * (a.x - accelFilt.x);
  accelFilt.y += k * (a.y - accelFilt.y);
  accelFilt.z += k * (a.z - accelFilt.z);
  tilt.compute(accelFilt, rollDeg, pitchDeg);
}

static void calibrateLevel() {
  Vec3 sum = {0, 0, 0};
  int n = 0;
  for (int i = 0; i < 50; ++i) {  // 1 s mitteln, Fahrzeug muss stehen
    Vec3 a;
    M5.Imu.update();
    if (M5.Imu.getAccel(&a.x, &a.y, &a.z)) {
      sum.x += a.x;
      sum.y += a.y;
      sum.z += a.z;
      ++n;
    }
    delay(20);
  }
  if (n < 10) return;
  settings.level[0] = sum.x / n;
  settings.level[1] = sum.y / n;
  settings.level[2] = sum.z / n;
  accelFilt = {settings.level[0], settings.level[1], settings.level[2]};
  clampSettings(settings);
  applySettings();
  saveSettings();
}

// ============================================================ Demo-Reifen
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
    if (w == RL) r.pressureBar = target - fmodf(t, 360.0f) * 0.0025f;  // HL verliert Luft
    r.temperatureC = 18.0f + w + 2.0f * sinf(t / 60.0f);
    r.batteryPct = w == RR ? 15 : 85;
    monitors[w].addReading(r, now);
  }
}

// ============================================================ Zeichen-Helfer
static bool blinkOn() { return (millis() / 400) % 2 == 0; }

static uint16_t sevColor(uint8_t sev) {
  if (sev >= 3) return C_CRIT;
  if (sev >= 1) return C_WARN;
  return C_OK;
}

static void text(const char* s, int x, int y, const lgfx::IFont* font, uint16_t color,
                 uint8_t datum = middle_center) {
  canvas.setFont(font);
  canvas.setTextDatum(datum);
  canvas.setTextColor(color);
  canvas.drawString(s, x, y);
}

// Rechteck um (cx,cy) gedreht zeichnen (Grad, im Uhrzeigersinn), k = Skalierung
static void fillRotRect(float cx, float cy, float ox, float oy, float w, float h, float deg, uint16_t col,
                        float k = 1.0f) {
  float s = sinf(deg * 0.0174533f), c = cosf(deg * 0.0174533f);
  float xs[4] = {ox, ox + w, ox + w, ox}, ys[4] = {oy, oy, oy + h, oy + h};
  int px[4], py[4];
  for (int i = 0; i < 4; ++i) {
    px[i] = (int)lroundf(cx + k * (xs[i] * c - ys[i] * s));
    py[i] = (int)lroundf(cy + k * (xs[i] * s + ys[i] * c));
  }
  canvas.fillTriangle(px[0], py[0], px[1], py[1], px[2], py[2], col);
  canvas.fillTriangle(px[0], py[0], px[2], py[2], px[3], py[3], col);
}

// Kopfzeile: Modus links, Titel/Status rechts
static void drawHeader(const char* right, uint16_t rightColor) {
  text(modeName(currentMode()), 8, 14, &fonts::FreeSansBold9pt7b, currentMode() == ROAD ? C_ROAD : C_OFF,
       middle_left);
  if (settings.demo) text("DEMO", 118, 14, &fonts::Font2, C_WARN, middle_left);
  if (right) text(right, W - 8, 14, &fonts::FreeSansBold9pt7b, rightColor, middle_right);
  canvas.drawFastHLine(0, 28, W, C_DARK);
}

static void drawPageDots() {
  for (int i = 0; i < PAGE_COUNT; ++i) {
    int x = W / 2 + (i - 1) * 14;
    if (i == page) canvas.fillCircle(x, 231, 3, C_TEXT);
    else canvas.drawCircle(x, 231, 3, C_DIM);
  }
}

// Bedienleiste unten mit drei Feldern (fuer Touch; beim Core2 liegen die Tasten-Punkte darunter)
static void drawBar(const char* l, const char* m, const char* r) {
  const char* lab[3] = {l, m, r};
  for (int i = 0; i < 3; ++i) {
    int x = i * (W / 3);
    canvas.fillRoundRect(x + 4, BAR_Y + 4, W / 3 - 8, H - BAR_Y - 8, 8, C_DARK);
    text(lab[i], x + W / 6, (BAR_Y + H) / 2, &fonts::FreeSansBold9pt7b, C_TEXT);
  }
}

// ============================================================ Seite: Reifen
static void drawTiresPage() {
  Wheel worst = alarms.worstWheel(states);
  uint8_t worstSev = worst == WHEEL_COUNT ? 0 : severity(states[worst]);

  // Statusmeldung oben rechts
  char status[24];
  uint16_t scol = C_DIM;
  if (worstSev >= 1) {
    snprintf(status, sizeof(status), "%s %s", wheelName(worst), stateText(states[worst]));
    scol = (worstSev >= 2 && !blinkOn()) ? C_BG : sevColor(worstSev);
  } else {
    bool any = false;
    for (int w = 0; w < WHEEL_COUNT; ++w) any |= states[w] == State::Ok;
    snprintf(status, sizeof(status), "%s", any ? "alles OK" : "keine Sensoren");
  }
  drawHeader(status, scol);

  // Transporter von oben (Fahrtrichtung nach oben)
  canvas.fillRoundRect(141, 42, 38, 170, 10, C_DARK);
  canvas.drawRoundRect(141, 42, 38, 170, 10, C_DIM);
  canvas.drawFastHLine(145, 64, 30, C_DIM);  // Windschutzscheibe
  canvas.drawFastHLine(145, 80, 30, C_DIM);  // Kabine
  const int wx[4] = {133, 180, 133, 180}, wy[4] = {52, 52, 170, 170};
  for (int w = 0; w < WHEEL_COUNT; ++w) {
    uint8_t sev = severity(states[w]);
    uint16_t col = (states[w] == State::NotPaired || states[w] == State::NoData) ? C_DIM : sevColor(sev);
    if (sev >= 2 && !blinkOn()) col = C_BG;
    canvas.fillRoundRect(wx[w], wy[w], 7, 28, 2, col);
  }

  // Werte: links rechtsbuendig an den Transporter, rechts linksbuendig
  const int vy[4] = {78, 78, 178, 178};
  for (int w = 0; w < WHEEL_COUNT; ++w) {
    const bool left = (w == FL || w == RL);
    const int x = left ? 120 : 200;
    const uint8_t datum = left ? middle_right : middle_left;
    State s = states[w];
    uint8_t sev = severity(s);
    char big[8], small[24];
    bool noVal = s == State::NotPaired || s == State::NoData || !monitors[w].hasData();
    if (noVal) strcpy(big, "--");
    else snprintf(big, sizeof(big), "%.1f", monitors[w].pressure());
    uint16_t col = noVal ? C_DIM : (sev >= 2 ? sevColor(sev) : C_TEXT);
    if (sev >= 2 && !blinkOn()) col = C_DARK;
    text(big, x, vy[w], &fonts::FreeSansBold24pt7b, col, datum);

    if (sev > 0 || noVal) snprintf(small, sizeof(small), "%s", stateText(s));
    else if (monitors[w].battery() < 20) snprintf(small, sizeof(small), "Batterie %u%%", (unsigned)monitors[w].battery());
    else snprintf(small, sizeof(small), "%.0fC  Soll %.1f", monitors[w].temperature(),
                  targetFor(settings, currentMode(), (Wheel)w));
    text(small, x, vy[w] + 32, &fonts::Font2, sev > 0 ? sevColor(sev) : C_DIM, datum);
  }
}

// ============================================================ Seite: Neigung
static void drawNoImu() {
  text("Kein Lagesensor gefunden", W / 2, 110, &fonts::FreeSansBold9pt7b, C_WARN);
  text("Core2/CoreS3 haben einen eingebaut;", W / 2, 138, &fonts::Font2, C_DIM);
  text("sonst Unit IMU an Port A stecken", W / 2, 156, &fonts::Font2, C_DIM);
}

static uint16_t tiltColor(float v, float warn) {
  float a = fabsf(v);
  return a >= warn ? C_CRIT : (a >= warn * 0.7f ? C_WARN : C_TEXT);
}

static void drawTiltPage() {
  drawHeader("NEIGUNG", C_DIM);
  if (!imuOk && !settings.demo) return drawNoImu();

  const float cxL = 85, cxR = 235, cy = 100, k = 1.4f;
  canvas.drawFastHLine(20, (int)(cy + 24 * k), 130, C_DARK);  // Horizont
  canvas.drawFastHLine(170, (int)(cy + 24 * k), 130, C_DARK);

  // Heckansicht: rechts tiefer = im Uhrzeigersinn
  uint16_t cr = tiltColor(rollDeg, settings.tiltWarnRoll);
  fillRotRect(cxL, cy, -24, -26, 48, 42, rollDeg, cr, k);
  fillRotRect(cxL, cy, -18, -20, 36, 10, rollDeg, C_BG, k);
  fillRotRect(cxL, cy, -24, 16, 12, 8, rollDeg, C_DIM, k);
  fillRotRect(cxL, cy, 12, 16, 12, 8, rollDeg, C_DIM, k);

  // Seitenansicht (Front rechts): Front hoeher = gegen den Uhrzeigersinn
  uint16_t cp = tiltColor(pitchDeg, settings.tiltWarnPitch);
  float pd = -pitchDeg;
  fillRotRect(cxR, cy, -36, -24, 66, 40, pd, cp, k);
  fillRotRect(cxR, cy, 30, -10, 10, 26, pd, cp, k);
  fillRotRect(cxR, cy, 18, -20, 12, 12, pd, C_BG, k);
  fillRotRect(cxR, cy, -30, 16, 12, 8, pd, C_DIM, k);
  fillRotRect(cxR, cy, 18, 16, 12, 8, pd, C_DIM, k);

  char buf[16];
  snprintf(buf, sizeof(buf), "%.0f", fabsf(rollDeg));
  text(buf, (int)cxL, 175, &fonts::FreeSansBold24pt7b, cr);
  snprintf(buf, sizeof(buf), "%.0f", fabsf(pitchDeg));
  text(buf, (int)cxR, 175, &fonts::FreeSansBold24pt7b, cp);
  text(fabsf(rollDeg) < 1 ? "Seite" : (rollDeg > 0 ? "rechts tiefer" : "links tiefer"), (int)cxL, 208,
       &fonts::Font2, C_DIM);
  text(fabsf(pitchDeg) < 1 ? "Laengs" : (pitchDeg > 0 ? "bergauf" : "bergab"), (int)cxR, 208, &fonts::Font2, C_DIM);
  text("Grad", W / 2, 175, &fonts::Font2, C_DIM);
  if (tiltWarn && blinkOn()) canvas.drawRect(0, 0, W, H, C_CRIT), canvas.drawRect(1, 1, W - 2, H - 2, C_CRIT);
}

// ============================================================ Seite: Ausrichten
static void drawLevelPage() {
  drawHeader("AUSRICHTEN", C_DIM);
  if (!imuOk && !settings.demo) return drawNoImu();

  // Libelle: Blase wandert zur hoeheren Seite
  const int cx = W / 2, cy = 122, r = 52;
  canvas.drawCircle(cx, cy, r, C_DIM);
  canvas.drawCircle(cx, cy, 11, C_DARK);
  canvas.drawFastHLine(cx - r, cy, 2 * r, C_DARK);
  canvas.drawFastVLine(cx, cy - r, 2 * r, C_DARK);
  float bx = -rollDeg * 8.0f, by = -pitchDeg * 8.0f;  // 8 px pro Grad
  float d = sqrtf(bx * bx + by * by);
  if (d > r - 10) {
    bx *= (r - 10) / d;
    by *= (r - 10) / d;
  }
  bool level = fabsf(rollDeg) < 0.5f && fabsf(pitchDeg) < 0.5f;
  canvas.fillCircle(cx + (int)bx, cy + (int)by, 10, level ? C_OK : C_WARN);

  float lift[WHEEL_COUNT];
  levelingLifts(rollDeg, pitchDeg, settings.wheelbaseCm, settings.trackCm, lift);
  const int lx[4] = {55, 265, 55, 265}, ly[4] = {75, 75, 165, 165};
  for (int w = 0; w < WHEEL_COUNT; ++w) {
    char buf[12];
    if (lift[w] < 0.5f) strcpy(buf, "0");
    else snprintf(buf, sizeof(buf), "+%.0f", lift[w]);
    text(buf, lx[w], ly[w], &fonts::FreeSansBold18pt7b, lift[w] < 0.5f ? C_OK : C_TEXT);
    text(wheelName((Wheel)w), lx[w], ly[w] + 26, &fonts::Font2, C_DIM);
  }
  if (level) text("EBEN", W / 2, 205, &fonts::FreeSansBold9pt7b, C_OK);
  else text("cm unter die Raeder legen", W / 2, 205, &fonts::Font2, C_DIM);
}

// ============================================================ Menue zeichnen
static void drawMenu() {
  text("Einstellungen", 8, 14, &fonts::FreeSansBold9pt7b, C_DIM, middle_left);
  canvas.drawFastHLine(0, 28, W, C_DARK);
  if (menuIdx < menuTop) menuTop = menuIdx;
  if (menuIdx >= menuTop + MENU_ROWS) menuTop = menuIdx - MENU_ROWS + 1;
  for (int r = 0; r < MENU_ROWS && menuTop + r < M_COUNT; ++r) {
    int i = menuTop + r, y = ROW_Y0 + r * ROW_H;
    if (i == menuIdx) canvas.fillRoundRect(4, y + 2, W - 8, ROW_H - 4, 8, C_DARK);
    text(MENU_LABELS[i], 14, y + ROW_H / 2, &fonts::FreeSans9pt7b, C_TEXT, middle_left);
    char val[20];
    menuValueText(i, val, sizeof(val));
    text(val, W - 14, y + ROW_H / 2, &fonts::FreeSansBold9pt7b, C_ROAD, middle_right);
  }
  drawBar("hoch", "zurueck", "runter");
}

static void drawEdit() {
  text(MENU_LABELS[menuIdx], W / 2, 50, &fonts::FreeSansBold9pt7b, C_DIM);
  char val[20];
  menuValueText(menuIdx, val, sizeof(val));
  text(val, W / 2, 115, &fonts::FreeSansBold24pt7b, C_TEXT);
  drawBar("-", "OK", "+");
}

static void drawPair() {
  char buf[40];
  snprintf(buf, sizeof(buf), "Sensoren anlernen %d/4", pairWheel + 1);
  text(buf, 8, 14, &fonts::FreeSansBold9pt7b, C_DIM, middle_left);
  canvas.drawFastHLine(0, 28, W, C_DARK);
  text(wheelName((Wheel)pairWheel), W / 2, 66, &fonts::FreeSansBold24pt7b, C_TEXT);
  if (pairHasCandidate) {
    char id[12];
    formatShortId(pairCandidate.id, id, sizeof(id));
    snprintf(buf, sizeof(buf), "Gefunden: %s  %s", sensorTypeName(pairCandidate.type), id);
    text(buf, W / 2, 118, &fonts::FreeSans9pt7b, C_TEXT);
    snprintf(buf, sizeof(buf), "%.2f bar   Signal %d dBm", pairCandidate.pressureBar, pairCandidate.rssi);
    text(buf, W / 2, 146, &fonts::FreeSans9pt7b, C_TEXT);
    drawBar("Abbruch", "", "Nehmen");
  } else {
    text("Sensor jetzt auf dieses Rad schrauben", W / 2, 118, &fonts::FreeSans9pt7b, C_TEXT);
    if (blinkOn()) text("suche ...", W / 2, 150, &fonts::FreeSans9pt7b, C_ROAD);
    drawBar("Abbruch", "", "Weiter");
  }
}

static void drawCalib() {
  text("Neigung nullen", 8, 14, &fonts::FreeSansBold9pt7b, C_DIM, middle_left);
  canvas.drawFastHLine(0, 28, W, C_DARK);
  text("Fahrzeug auf ebener Flaeche abstellen,", W / 2, 60, &fonts::FreeSans9pt7b, C_TEXT);
  text("dann Speichern tippen.", W / 2, 84, &fonts::FreeSans9pt7b, C_TEXT);
  char buf[32];
  snprintf(buf, sizeof(buf), "Seite %.1f   Laengs %.1f", rollDeg, pitchDeg);
  text(buf, W / 2, 140, &fonts::FreeSansBold12pt7b, C_DIM);
  drawBar("Abbruch", "", "Speichern");
}

static void drawOverlay() {
  if (!overlayText || millis() >= overlayUntil) return;
  canvas.fillRoundRect(50, 80, W - 100, 80, 14, C_BG);
  canvas.drawRoundRect(50, 80, W - 100, 80, 14, C_DIM);
  text(overlayText, W / 2, 120, &fonts::FreeSansBold18pt7b, C_TEXT);
}

// ============================================================ Eingabe
// Alle Eingaben (Touch, Core2-Tasten) werden auf wenige Ereignisse abgebildet.
enum class Ev : uint8_t { None, Left, Mid, Right, Hold, Next, Prev, Row };

static Ev readInput(int& row) {
  row = -1;
  // Core2: die drei Tasten-Punkte unter dem Display (bei CoreS3 gibt es sie nicht)
  if (M5.BtnA.wasClicked()) return Ev::Left;
  if (M5.BtnB.wasHold()) return Ev::Hold;
  if (M5.BtnB.wasClicked()) return Ev::Mid;
  if (M5.BtnC.wasClicked()) return Ev::Right;

  if (!M5.Touch.getCount()) return Ev::None;
  const auto& t = M5.Touch.getDetail();
  if (t.y >= H) return Ev::None;  // Tastenfeld des Core2, schon oben behandelt
  if (t.wasHold()) return Ev::Hold;
  if (t.wasFlicked()) {
    int dx = t.distanceX(), dy = t.distanceY();
    if (abs(dx) > abs(dy)) return dx < 0 ? Ev::Next : Ev::Prev;
    return dy < 0 ? Ev::Next : Ev::Prev;
  }
  if (!t.wasClicked()) return Ev::None;

  const bool inBar = screen != Screen::Main && t.y >= BAR_Y;
  if (screen == Screen::Menu && !inBar && t.y >= ROW_Y0) {
    row = (t.y - ROW_Y0) / ROW_H;
    if (row < MENU_ROWS) return Ev::Row;
  }
  if (screen != Screen::Main && !inBar) return Ev::None;
  if (t.x < W / 3) return Ev::Left;
  if (t.x >= 2 * W / 3) return Ev::Right;
  return Ev::Mid;
}

static void activateMenuItem() {
  click();
  switch (menuIdx) {
    case M_BACK: screen = Screen::Main; break;
    case M_PAIR:
      pairWheel = 0;
      pairHasCandidate = false;
      screen = Screen::Pair;
      break;
    case M_CALIB:
      if (imuOk) screen = Screen::Calib;
      else showOverlay("kein Sensor");
      break;
    case M_SOUND:
      settings.buzzer = !settings.buzzer;
      saveSettings();
      break;
    case M_DEMO:
      settings.demo = !settings.demo;
      for (auto& m : monitors) m.reset();
      saveSettings();
      break;
    default: screen = Screen::Edit;
  }
}

static void leaveEdit() {
  clampSettings(settings);
  applySettings();
  saveSettings();
  click();
  screen = Screen::Menu;
}

static void handleInput() {
  int row;
  Ev ev = readInput(row);
  uint32_t now = millis();
  if (ev != Ev::None) lastInputMs = now;

  switch (screen) {
    case Screen::Main:
      if (ev == Ev::Left || ev == Ev::Prev) page = (page + PAGE_COUNT - 1) % PAGE_COUNT, click();
      else if (ev == Ev::Right || ev == Ev::Next) page = (page + 1) % PAGE_COUNT, click();
      else if (ev == Ev::Mid) {
        if (alarms.hasUnacknowledged()) {
          alarms.acknowledgeAll();
          beeper.stop();
        } else {
          settings.mode = currentMode() == ROAD ? OFFROAD : ROAD;
          saveSettings();
          showOverlay(modeName(currentMode()));
          click();
        }
      } else if (ev == Ev::Hold) {
        screen = Screen::Menu;
        menuIdx = menuTop = 0;
        click();
      }
      break;

    case Screen::Menu:
      if (ev == Ev::Left || ev == Ev::Prev) menuIdx = addClamped(menuIdx, -1, 0, M_COUNT - 1), click();
      else if (ev == Ev::Right || ev == Ev::Next) menuIdx = addClamped(menuIdx, 1, 0, M_COUNT - 1), click();
      else if (ev == Ev::Row && menuTop + row < M_COUNT) {
        menuIdx = menuTop + row;
        activateMenuItem();
      } else if (ev == Ev::Mid || ev == Ev::Hold || now - lastInputMs > MENU_TIMEOUT_MS) {
        screen = Screen::Main;
      }
      break;

    case Screen::Edit:
      if (ev == Ev::Left || ev == Ev::Prev) menuAdjust(menuIdx, -1), click();
      else if (ev == Ev::Right || ev == Ev::Next) menuAdjust(menuIdx, 1), click();
      else if (ev == Ev::Mid || ev == Ev::Hold || now - lastInputMs > MENU_TIMEOUT_MS) leaveEdit();
      break;

    case Screen::Pair:
      if (ev == Ev::Right) pairingNext(true);
      else if (ev == Ev::Left || ev == Ev::Hold) {
        saveSensors();
        screen = Screen::Main;
      }
      break;

    case Screen::Calib:
      if (ev == Ev::Right) {
        calibrateLevel();
        beeper.play(BEEP_OK, 4, false, 4000);
        showOverlay("gespeichert");
        screen = Screen::Main;
        page = P_LEVEL;
      } else if (ev == Ev::Left || ev == Ev::Hold) {
        screen = Screen::Menu;
      }
      break;
  }
}

// ============================================================ Auswertung
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
    case AlarmManager::Sound::Alarm:
      if (!beeper.repeat && screen == Screen::Main) page = P_TIRES;  // Alarm hat Vorrang
      beeper.play(BEEP_ALARM, 6, true, 2800);
      break;
    case AlarmManager::Sound::WarnOnce: beeper.play(BEEP_WARN, 4, false, 3200); break;
    case AlarmManager::Sound::None:
      if (beeper.repeat && !alarms.hasUnacknowledged()) beeper.stop();
      break;
  }

  if (imuOk || settings.demo) {
    bool w = tiltWarning(tiltWarn, rollDeg, pitchDeg, settings.tiltWarnRoll, settings.tiltWarnPitch);
    if (w && !tiltWarn && !alarms.hasUnacknowledged()) {
      if (screen == Screen::Main) page = P_TILT;
      beeper.play(BEEP_TILT, 6, false, 4500);
    }
    tiltWarn = w;
  }
}

// ============================================================ Arduino
void setup() {
  auto cfg = M5.config();
  cfg.external_imu = true;  // falls kein interner Sensor: Unit IMU an Port A suchen
  M5.begin(cfg);
  M5.BtnB.setHoldThresh(800);
  M5.Touch.setHoldThresh(800);
  M5.Display.setRotation(1);  // quer
  Serial.begin(115200);

  loadAll();
  applySettings();
  imuOk = M5.Imu.isEnabled();
  accelFilt = {settings.level[0], settings.level[1], settings.level[2]};

  canvas.setColorDepth(16);
  canvas.createSprite(W, H);
  canvas.fillScreen(C_BG);
  text("Reifendruck", W / 2, 105, &fonts::FreeSansBold18pt7b, C_TEXT);
  text(imuOk ? "+ Neigung" : "ohne Lagesensor", W / 2, 145, &fonts::Font2, C_DIM);
  canvas.pushSprite(0, 0);

  blet::begin();
  Serial.printf("Start. Lagesensor: %s\n", imuOk ? "ja" : "nein");
}

void loop() {
  M5.update();
  uint32_t now = millis();
  handleInput();
  processReadings(now);
  if (settings.demo) demoTick(now);
  updateTilt();

  static uint32_t lastEval = 0, lastDraw = 0, lastKeep = 0;
  if (now - lastEval >= 250) {
    lastEval = now;
    evaluateAll(now);
  }
  if (now - lastKeep >= 10000) {
    lastKeep = now;
    blet::keepAlive();
    Serial.printf("[%s] roll %.1f pitch %.1f |", modeName(currentMode()), rollDeg, pitchDeg);
    for (int w = 0; w < WHEEL_COUNT; ++w)
      Serial.printf(" %s %.2f %s", wheelName((Wheel)w), monitors[w].pressure(), stateText(states[w]));
    Serial.println();
  }
  beeper.update();

  if (now - lastDraw >= 50) {  // ~20 fps
    lastDraw = now;
    canvas.fillScreen(C_BG);
    switch (screen) {
      case Screen::Main:
        if (page == P_TIRES) drawTiresPage();
        else if (page == P_TILT) drawTiltPage();
        else drawLevelPage();
        drawPageDots();
        break;
      case Screen::Menu: drawMenu(); break;
      case Screen::Edit: drawEdit(); break;
      case Screen::Pair: drawPair(); break;
      case Screen::Calib: drawCalib(); break;
    }
    drawOverlay();
    canvas.pushSprite(0, 0);
  }
}
