// Bordinstrument fuer den Sprinter W906 – M5Stack DinMeter v1.1, Retro-Design in Bernstein-Gelb
//
// Hardware: M5Stack DinMeter v1.1 (ESP32-S3, 1,14"-Display 240x135, Drehknopf, Summer, Uhr,
//           6–36 V, Einbau im 1/32-DIN-Ausschnitt 45 x 22,5 mm)
//           + 4 BLE-Ventilkappen-Sensoren
//           + M5Stack Unit Mini IMU-Pro an Port A (Lage, Kompass, Luftdruck) – optional
//
// Seiten (Knopf drehen):
//   DRUCK -> REIFENTEMP -> NEIGUNG -> KOMPASS -> BORD (Hoehe/Wetter/Uhr) -> AUSRICHTEN
// Knopf kurz: Strasse <-> Offroad (bei Alarm: quittieren)      Knopf lang: Menue
//
// Gestaltung: alles in einer Farbe (Bernstein), Zahlen als 7-Segment mit schwach sichtbaren
// unbeleuchteten Segmenten, Beschriftung in Pixelschrift, Warnungen invertiert.
// Vorschau aller Seiten: docs/bilder/uebersicht.png

#include <M5DinMeter.h>
#include <Preferences.h>

#include "BleTpms.h"
#include "TpmsCore.h"

using namespace tpms;

#ifndef ENC_COUNTS_PER_DETENT  // springt der Knopf 2 Eintraege pro Klick: auf 2 setzen
#define ENC_COUNTS_PER_DETENT 4
#endif
#define MENU_TIMEOUT_MS 30000
#define OVERLAY_MS 1500
#define W 240
#define H 135

// ============================================================ Farben (RGB565)
static const uint16_t BG = 0x0000;
static const uint16_t AMB = 0xFDA0;    // Bernstein hell
static const uint16_t MID = 0xABC0;    // Bernstein gedimmt (Beschriftung)
static const uint16_t GHOST = 0x1880;  // unbeleuchtete Segmente
static const uint16_t NOGHOST = 0x0001;  // Marker: keine Geister-Segmente zeichnen

static M5Canvas canvas(&M5.Display);

// ============================================================ Zustand
static Settings settings;
static uint64_t sensorIds[WHEEL_COUNT] = {0, 0, 0, 0};
static WheelMonitor monitors[WHEEL_COUNT];
static State states[WHEEL_COUNT] = {State::NotPaired, State::NotPaired, State::NotPaired, State::NotPaired};
static AlarmManager alarms;
static Preferences prefs;

// Lage / Kompass
static TiltCalc tilt;
static bool imuOk = false, magOk = false;
static Vec3 accelFilt = {0, 0, 1};
static Vec3 magRaw = {0, 0, 0};
static float rollDeg = 0, pitchDeg = 0, headingDeg = 0;
static float headSin = 0, headCos = 1;  // geglaetteter Kurs
static bool tiltWarn = false;
static MagCalibrator magCal;

// Luftdruck / Uhr
static uint8_t bmpAddr = 0;
static Bmp280Calib bmpCal;
static float airTempC = NAN, airPa = NAN;
static PressureTrend trend;
static bool rtcOk = false;

enum class Screen : uint8_t { Main, Menu, Edit, Pair, Calib, MagCalib };
enum Page : uint8_t { P_TIRES, P_TEMPS, P_TILT, P_COMPASS, P_BORD, P_LEVEL, PAGE_COUNT };
static Screen screen = Screen::Main;
static int page = P_TIRES;
static uint32_t overlayUntil = 0;
static const char* overlayText = nullptr;
static uint32_t lastInputMs = 0;

static Mode currentMode() { return (Mode)settings.mode; }
static bool isPaired(int w) { return settings.demo || sensorIds[w] != 0; }
static bool haveTilt() { return imuOk || settings.demo; }
static bool haveMag() { return magOk || settings.demo; }
static bool haveAir() { return bmpAddr != 0 || settings.demo; }

static void showOverlay(const char* text) {
  overlayText = text;
  overlayUntil = millis() + OVERLAY_MS;
}

// ============================================================ Speicher
static const uint8_t SETTINGS_VERSION = 3;

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
  if (!beeper.repeat && settings.buzzer) M5.Speaker.tone(6000, 12);
}

// ============================================================ Zeichnen: Grundbausteine
static bool blinkOn() { return (millis() / 400) % 2 == 0; }

enum Align : uint8_t { AL_L, AL_R, AL_C };

static void upcase(char* s) {
  for (; *s; ++s)
    if (*s >= 'a' && *s <= 'z') *s -= 32;
}

// Pixelschrift (6x8-Zeichenzelle, size = Vergroesserung), immer in Grossbuchstaben
static void ptext(const char* s, int x, int y, int size, uint16_t col, Align al = AL_L) {
  char buf[40];
  snprintf(buf, sizeof(buf), "%s", s);
  upcase(buf);
  int w = (int)strlen(buf) * 6 * size;
  if (al == AL_R) x -= w;
  else if (al == AL_C) x -= w / 2;
  canvas.setFont(&fonts::Font0);
  canvas.setTextSize(size);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(col);
  canvas.drawString(buf, x, y);
  canvas.setTextSize(1);
}

// 7-Segment-Ziffern. Unterstuetzt 0-9, '-', ' ', '.', ':'
static void segMetrics(int h, int& w, int& t, int& sp) {
  t = h / 9 < 2 ? 2 : h / 9;
  w = h / 2;
  sp = t + 2;
}

static int segWidth(const char* s, int h) {
  int w, t, sp, tot = 0;
  segMetrics(h, w, t, sp);
  for (const char* p = s; *p; ++p) tot += (*p == '.' || *p == ':') ? t + sp : w + sp;
  return tot > 0 ? tot - sp : 0;
}

static uint8_t segBits(char c) {
  switch (c) {
    case '0': return 0x3F;
    case '1': return 0x06;
    case '2': return 0x5B;
    case '3': return 0x4F;
    case '4': return 0x66;
    case '5': return 0x6D;
    case '6': return 0x7D;
    case '7': return 0x07;
    case '8': return 0x7F;
    case '9': return 0x6F;
    case '-': return 0x40;
    default: return 0;
  }
}

static void seg(const char* s, int x, int y, int h, uint16_t on, uint16_t ghost = GHOST, Align al = AL_L) {
  int w, t, sp;
  segMetrics(h, w, t, sp);
  int tw = segWidth(s, h);
  if (al == AL_R) x -= tw;
  else if (al == AL_C) x -= tw / 2;
  for (const char* p = s; *p; ++p) {
    if (*p == '.') {
      canvas.fillRect(x, y + h - t, t, t, on);
      x += t + sp;
      continue;
    }
    if (*p == ':') {
      canvas.fillRect(x, y + h / 3 - t / 2, t, t, on);
      canvas.fillRect(x, y + 2 * h / 3 - t / 2, t, t, on);
      x += t + sp;
      continue;
    }
    uint8_t bits = segBits(*p);
    int mid = y + h / 2 - t / 2;
    int r[7][4] = {{x + t, y, w - 2 * t, t},
                   {x + w - t, y + t, t, mid - (y + t)},
                   {x + w - t, mid + t, t, (y + h - t) - (mid + t)},
                   {x + t, y + h - t, w - 2 * t, t},
                   {x, mid + t, t, (y + h - t) - (mid + t)},
                   {x, y + t, t, mid - (y + t)},
                   {x + t, mid, w - 2 * t, t}};
    for (int i = 0; i < 7; ++i) {
      uint16_t col = (bits >> i) & 1 ? on : ghost;
      if (col != NOGHOST) canvas.fillRect(r[i][0], r[i][1], r[i][2], r[i][3], col);
    }
    x += w + sp;
  }
}

static void dottedH(int x0, int x1, int y, int step, uint16_t col) {
  for (int x = x0; x <= x1; x += step) canvas.drawPixel(x, y, col);
}
static void dottedV(int x, int y0, int y1, int step, uint16_t col) {
  for (int y = y0; y <= y1; y += step) canvas.drawPixel(x, y, col);
}

static void header(const char* left, const char* right = nullptr, uint16_t rcol = MID) {
  ptext(left, 1, 1, 1, AMB);
  if (right) ptext(right, 239, 1, 1, rcol, AL_R);
  dottedH(0, W - 1, 11, 3, MID);
}

// Polygon um (cx,cy) drehen (Grad, im Uhrzeigersinn) und als Drahtmodell zeichnen
static void wirePoly(float cx, float cy, const int8_t (*pts)[2], int n, float deg, float k, uint16_t col) {
  float s = sinf(deg * 0.0174533f), c = cosf(deg * 0.0174533f);
  int px[8], py[8];
  for (int i = 0; i < n && i < 8; ++i) {
    px[i] = (int)lroundf(cx + k * (pts[i][0] * c - pts[i][1] * s));
    py[i] = (int)lroundf(cy + k * (pts[i][0] * s + pts[i][1] * c));
  }
  for (int i = 0; i < n; ++i) canvas.drawLine(px[i], py[i], px[(i + 1) % n], py[(i + 1) % n], col);
}

// ============================================================ Transporter quer (Front rechts)
// Bildschirm: rechts = vorne, oben = linke Fahrzeugseite.  -> VL oben rechts, VR unten rechts,
// HL oben links, HR unten links
static const int QX[4] = {196, 196, 44, 44};
static const bool QTOP[4] = {true, false, true, false};

static void drawVan(int cx, int cy) {
  const int x0 = cx - 28, y0 = cy - 17, w = 56, h = 34;
  canvas.drawRoundRect(x0, y0, w, h, 4, AMB);
  canvas.drawFastVLine(x0 + 42, y0 + 3, h - 6, AMB);  // Windschutzscheibe
  canvas.drawFastVLine(x0 + 36, y0 + 3, h - 6, MID);  // Kabine
  canvas.fillTriangle(x0 + w + 3, cy - 4, x0 + w + 8, cy, x0 + w + 3, cy + 4, MID);  // Fahrtrichtung
  const int wx[4] = {x0 + 40, x0 + 40, x0 + 6, x0 + 6}, wy[4] = {y0 - 4, y0 + h, y0 - 4, y0 + h};
  for (int i = 0; i < WHEEL_COUNT; ++i) {
    bool alarm = severity(states[i]) >= 2;
    if (alarm && !blinkOn()) canvas.drawRect(wx[i], wy[i], 12, 4, AMB);
    else canvas.fillRect(wx[i], wy[i], 12, 4, AMB);
  }
}

// Ein Rad-Quadrant: Beschriftung aussen, 7-Segment-Wert in der Mitte, optional invertiert
static void drawQuadrant(int w, const char* label, const char* value, bool invert, int segH = 44) {
  const bool top = QTOP[w];
  const int bx = QX[w] < 120 ? 0 : 152, by = top ? 13 : 72, bh = top ? 59 : 63;
  uint16_t fg = AMB, gh = GHOST, lab = MID;
  if (invert) {
    canvas.fillRect(bx, by, 88, bh, AMB);
    fg = BG;
    gh = NOGHOST;
    lab = BG;
  }
  ptext(label, bx + 4, top ? 15 : 124, 1, lab);
  seg(value, QX[w], top ? 25 : 76, segH, fg, gh, AL_C);
}

// ============================================================ Seiten
static void drawTiresPage() {
  Wheel worst = alarms.worstWheel(states);
  uint8_t worstSev = worst == WHEEL_COUNT ? 0 : severity(states[worst]);
  char left[24], right[24];
  snprintf(left, sizeof(left), "DRUCK  %s", modeName(currentMode()));
  if (worstSev >= 1) {
    snprintf(right, sizeof(right), "!! %s %s", wheelName(worst), stateText(states[worst]));
    header(left, right, (worstSev >= 2 && !blinkOn()) ? BG : AMB);
  } else {
    header(left, "BAR");
  }
  drawVan(120, 67);

  for (int w = 0; w < WHEEL_COUNT; ++w) {
    State s = states[w];
    uint8_t sev = severity(s);
    bool noVal = s == State::NotPaired || s == State::NoData || !monitors[w].hasData();
    char val[8], lab[24];
    if (noVal) strcpy(val, "--");
    else snprintf(val, sizeof(val), "%.1f", monitors[w].pressure());
    if (sev > 0 || noVal) snprintf(lab, sizeof(lab), "%s %s", wheelName((Wheel)w), stateText(s));
    else if (monitors[w].battery() < 20) snprintf(lab, sizeof(lab), "%s BATT %u%%", wheelName((Wheel)w), (unsigned)monitors[w].battery());
    else snprintf(lab, sizeof(lab), "%s SOLL %.1f", wheelName((Wheel)w), targetFor(settings, currentMode(), (Wheel)w));
    drawQuadrant(w, lab, val, sev >= 2 && blinkOn());
  }
}

static void drawTempsPage() {
  header("REIFENTEMP  GRAD C", "SENSOR-BATT");
  drawVan(120, 67);
  for (int w = 0; w < WHEEL_COUNT; ++w) {
    bool noVal = states[w] == State::NotPaired || states[w] == State::NoData || !monitors[w].hasData();
    char val[8], lab[24];
    if (noVal) strcpy(val, "--");
    else snprintf(val, sizeof(val), "%d", (int)lroundf(monitors[w].temperature()));
    if (noVal) snprintf(lab, sizeof(lab), "%s %s", wheelName((Wheel)w), stateText(states[w]));
    else snprintf(lab, sizeof(lab), "%s BATT %u%%", wheelName((Wheel)w), (unsigned)monitors[w].battery());
    drawQuadrant(w, lab, val, false);
  }
}

static void drawNoSensor(const char* title, const char* what) {
  header(title);
  ptext(what, W / 2, 50, 2, AMB, AL_C);
  ptext("UNIT MINI IMU-PRO AN PORT A", W / 2, 80, 1, MID, AL_C);
  ptext("STECKEN, NEU STARTEN", W / 2, 92, 1, MID, AL_C);
}

static void drawTiltPage() {
  if (!haveTilt()) return drawNoSensor("NEIGUNG", "KEIN LAGESENSOR");
  header("NEIGUNG", "GRAD");
  dottedH(4, 115, 62, 4, MID);
  dottedH(124, 235, 62, 4, MID);
  uint16_t cr = (tiltWarn && fabsf(rollDeg) >= settings.tiltWarnRoll - 2 && !blinkOn()) ? BG : AMB;
  uint16_t cp = (tiltWarn && fabsf(pitchDeg) >= settings.tiltWarnPitch - 2 && !blinkOn()) ? BG : AMB;

  // Heckansicht (dreht mit der Seitenneigung: rechts tiefer = im Uhrzeigersinn)
  static const int8_t rearBody[4][2] = {{-24, -24}, {24, -24}, {26, 16}, {-26, 16}};
  static const int8_t rearWin[4][2] = {{-18, -18}, {18, -18}, {18, -8}, {-18, -8}};
  static const int8_t rearWl[4][2] = {{-22, 16}, {-12, 16}, {-12, 24}, {-22, 24}};
  static const int8_t rearWr[4][2] = {{12, 16}, {22, 16}, {22, 24}, {12, 24}};
  wirePoly(60, 42, rearBody, 4, rollDeg, 0.8f, cr);
  wirePoly(60, 42, rearWin, 4, rollDeg, 0.8f, cr);
  wirePoly(60, 42, rearWl, 4, rollDeg, 0.8f, cr);
  wirePoly(60, 42, rearWr, 4, rollDeg, 0.8f, cr);

  // Seitenansicht, Front rechts (Front hoeher = gegen den Uhrzeigersinn)
  static const int8_t sideBody[6][2] = {{-38, -24}, {18, -24}, {30, -10}, {40, -6}, {40, 16}, {-38, 16}};
  static const int8_t sideWin[3][2] = {{18, -20}, {26, -10}, {18, -10}};
  static const int8_t sideWr[4][2] = {{-30, 16}, {-18, 16}, {-18, 24}, {-30, 24}};
  static const int8_t sideWf[4][2] = {{20, 16}, {32, 16}, {32, 24}, {20, 24}};
  float pd = -pitchDeg;
  wirePoly(180, 42, sideBody, 6, pd, 0.8f, cp);
  wirePoly(180, 42, sideWin, 3, pd, 0.8f, cp);
  wirePoly(180, 42, sideWr, 4, pd, 0.8f, cp);
  wirePoly(180, 42, sideWf, 4, pd, 0.8f, cp);

  char buf[8];
  snprintf(buf, sizeof(buf), "%.0f", fabsf(rollDeg));
  seg(buf, 60, 70, 44, cr, GHOST, AL_C);
  snprintf(buf, sizeof(buf), "%.0f", fabsf(pitchDeg));
  seg(buf, 180, 70, 44, cp, GHOST, AL_C);
  ptext(fabsf(rollDeg) < 1 ? "SEITE" : (rollDeg > 0 ? "SEITE R-TIEF" : "SEITE L-TIEF"), 60, 124, 1, MID, AL_C);
  ptext(fabsf(pitchDeg) < 1 ? "LAENGS" : (pitchDeg > 0 ? "BERGAUF" : "BERGAB"), 180, 124, 1, MID, AL_C);
}

static void drawCompassPage() {
  if (!haveMag()) return drawNoSensor("KOMPASS", "KEIN MAGNETSENSOR");
  header("KOMPASS", settings.magCalibrated || settings.demo ? "KURS" : "BITTE KALIBRIEREN",
         settings.magCalibrated || settings.demo ? MID : AMB);
  int hdg = ((int)lroundf(headingDeg)) % 360;
  char buf[8];
  snprintf(buf, sizeof(buf), "%03d", hdg);
  seg(buf, 100, 18, 48, AMB, GHOST, AL_C);
  ptext(compassPoint((float)hdg), 200, 30, 3, AMB, AL_C);

  // Kompassband
  const int y0 = 86;
  canvas.drawFastHLine(0, y0, W, MID);
  canvas.drawFastHLine(0, y0 + 40, W, MID);
  for (int d = hdg - 70; d <= hdg + 70; ++d) {
    int x = 120 + (int)lroundf((d - hdg) * 1.6f);
    int dd = ((d % 360) + 360) % 360;
    if (dd % 10 == 0) canvas.drawFastVLine(x, y0 + 30, 10, dd % 30 == 0 ? AMB : MID);
    if (dd % 45 == 0) ptext(compassPoint((float)dd), x, y0 + 8, 2, AMB, AL_C);
  }
  canvas.fillTriangle(114, y0 + 1, 126, y0 + 1, 120, y0 + 8, AMB);
}

static void drawBordPage() {
  char clock[8] = "--:--";
  m5::rtc_time_t t;
  if (rtcOk && M5.Rtc.getTime(&t) && t.hours >= 0) snprintf(clock, sizeof(clock), "%02d:%02d", t.hours, t.minutes);
  header("BORD", clock);

  char buf[12];
  // Hoehe
  ptext("HOEHE M", 4, 14, 1, MID);
  if (haveAir() && !isnan(airPa)) snprintf(buf, sizeof(buf), "%d", (int)lroundf(altitudeFromPressure(airPa, settings.qnhPa)));
  else strcpy(buf, "---");
  seg(buf, 4, 25, 36, AMB);
  // Uhr
  ptext("UHR", 236, 14, 1, MID, AL_R);
  seg(clock, 236, 25, 36, AMB, GHOST, AL_R);
  // Luftdruck
  ptext("LUFTDRUCK HPA", 4, 76, 1, MID);
  if (haveAir() && !isnan(airPa)) snprintf(buf, sizeof(buf), "%d", (int)lroundf(airPa / 100.0f));
  else strcpy(buf, "----");
  seg(buf, 4, 87, 36, AMB);
  // Temperatur
  ptext("INNEN GRAD C", 236, 76, 1, MID, AL_R);
  if (haveAir() && !isnan(airTempC)) snprintf(buf, sizeof(buf), "%d", (int)lroundf(airTempC));
  else strcpy(buf, "--");
  seg(buf, 236, 87, 36, AMB, GHOST, AL_R);

  char tr[24];
  snprintf(tr, sizeof(tr), "TREND: %s", trend.text());
  ptext(tr, 4, 126, 1, AMB);
}

static void drawLevelPage() {
  if (!haveTilt()) return drawNoSensor("AUSRICHTEN", "KEIN LAGESENSOR");
  bool level = fabsf(rollDeg) < 0.5f && fabsf(pitchDeg) < 0.5f;
  header("AUSRICHTEN", level ? "EBEN!" : "CM UNTERLEGEN", level ? AMB : MID);

  // Libelle (Front rechts): Blase wandert zur hoeheren Seite
  const int cx = 120, cy = 73, r = 40;
  canvas.drawCircle(cx, cy, r, AMB);
  canvas.drawCircle(cx, cy, 8, MID);
  dottedH(cx - r, cx + r, cy, 4, MID);
  dottedV(cx, cy - r, cy + r, 4, MID);
  float bx = pitchDeg * 6.0f, by = -rollDeg * 6.0f;
  float d = sqrtf(bx * bx + by * by);
  if (d > r - 8) {
    bx *= (r - 8) / d;
    by *= (r - 8) / d;
  }
  canvas.fillCircle(cx + (int)bx, cy + (int)by, 7, AMB);

  float lift[WHEEL_COUNT];
  levelingLifts(rollDeg, pitchDeg, settings.wheelbaseCm, settings.trackCm, lift);
  for (int w = 0; w < WHEEL_COUNT; ++w) {
    char val[8], lab[12];
    snprintf(val, sizeof(val), "%d", lift[w] < 0.5f ? 0 : (int)lroundf(lift[w]));
    snprintf(lab, sizeof(lab), "%s CM", wheelName((Wheel)w));
    ptext(lab, QX[w], QTOP[w] ? 15 : 124, 1, MID, AL_C);
    seg(val, QX[w], QTOP[w] ? 26 : 78, 36, AMB, GHOST, AL_C);
  }
}

// ============================================================ Menue
enum MenuItem : uint8_t {
  M_ROAD_F, M_ROAD_R, M_OFF_F, M_OFF_R, M_WARN, M_CRIT, M_HIGH, M_LEAK_BAR, M_LEAK_MIN, M_PAIR,
  M_CALIB, M_MOUNT, M_TILT_R, M_TILT_P, M_MAGCAL, M_MAGOFF, M_MAGMIR, M_ALT, M_HOUR, M_MIN,
  M_WB, M_TRACK, M_BRIGHT, M_VOL, M_SOUND, M_DEMO, M_BACK, M_COUNT
};
static const char* const MENU_LABELS[M_COUNT] = {
    "SOLL STR VO", "SOLL STR HI", "SOLL OFF VO", "SOLL OFF HI", "WARNUNG",    "KRITISCH",   "ZU HOCH",
    "LECK BAR",    "LECK MIN",    "SENSOREN",    "NEIGUNG 0",   "EINBAULAGE", "WARN SEITE", "WARN LAENGS",
    "KOMPASS KAL", "KOMPASS +-",  "KOMP SPIEG",  "HOEHE",       "UHR STD",    "UHR MIN",    "RADSTAND",
    "SPURWEITE",   "HELLIGKEIT",  "LAUTSTAERKE", "WARNTON",     "DEMO",       "ZURUECK"};
// Ausfuehrliche Titel fuer den Bearbeiten-Bildschirm
static const char* const EDIT_TITLES[M_COUNT] = {
    "SOLL STRASSE VORNE",  "SOLL STRASSE HINTEN", "SOLL OFFROAD VORNE", "SOLL OFFROAD HINTEN",
    "WARNUNG UNTER SOLL",  "KRITISCH UNTER SOLL", "HINWEIS UEBER SOLL", "LECK: DRUCKVERLUST",
    "LECK: ZEITRAUM",      "",                    "",                   "EINBAULAGE LAGESENSOR",
    "WARNUNG SEITENNEIG.", "WARNUNG LAENGSNEIG.", "",                   "KOMPASS KORREKTUR",
    "",                    "AKTUELLE HOEHE",      "UHR STUNDE",         "UHR MINUTE",
    "RADSTAND",            "SPURWEITE",           "HELLIGKEIT",         "LAUTSTAERKE",
    "",                    "",                    ""};
static int menuIdx = 0, menuTop = 0;
static const int MENU_ROWS = 5;
static float editAltitude = 0;  // beim Bearbeiten der Hoehe

static float* targetPtr(int item) {
  switch (item) {
    case M_ROAD_F: return &settings.target[ROAD][0];
    case M_ROAD_R: return &settings.target[ROAD][1];
    case M_OFF_F: return &settings.target[OFFROAD][0];
    case M_OFF_R: return &settings.target[OFFROAD][1];
    default: return nullptr;
  }
}

static int rtcField(bool hour) {
  m5::rtc_time_t t;
  if (!rtcOk || !M5.Rtc.getTime(&t) || t.hours < 0) return 0;
  return hour ? t.hours : t.minutes;
}

// Wert als kurzer Text (Menue) bzw. als 7-Segment-tauglicher Text + Einheit (Editor)
static void menuValue(int item, char* buf, size_t n, char* unit = nullptr, size_t un = 0) {
  auto setUnit = [&](const char* u) {
    if (unit) snprintf(unit, un, "%s", u);
  };
  setUnit("");
  if (float* t = targetPtr(item)) {
    snprintf(buf, n, "%.1f", *t);
    setUnit("BAR");
    return;
  }
  switch (item) {
    case M_WARN: snprintf(buf, n, "-%u", (unsigned)settings.warnPct), setUnit("%"); break;
    case M_CRIT: snprintf(buf, n, "-%u", (unsigned)settings.critPct), setUnit("%"); break;
    case M_HIGH: snprintf(buf, n, "%u", (unsigned)settings.highPct), setUnit("% UEBER"); break;
    case M_LEAK_BAR: snprintf(buf, n, "%.1f", settings.leakDeciBar / 10.0f), setUnit("BAR"); break;
    case M_LEAK_MIN: snprintf(buf, n, "%u", (unsigned)settings.leakMinutes), setUnit("MIN"); break;
    case M_MOUNT: snprintf(buf, n, "%u", (unsigned)settings.mountQuarter * 90), setUnit("GRAD"); break;
    case M_TILT_R: snprintf(buf, n, "%u", (unsigned)settings.tiltWarnRoll), setUnit("GRAD"); break;
    case M_TILT_P: snprintf(buf, n, "%u", (unsigned)settings.tiltWarnPitch), setUnit("GRAD"); break;
    case M_MAGCAL: snprintf(buf, n, "%s", settings.magCalibrated ? "OK" : "--"); break;
    case M_MAGOFF: snprintf(buf, n, "%d", (int)settings.compassOffsetDeg), setUnit("GRAD"); break;
    case M_MAGMIR: snprintf(buf, n, "%s", settings.compassMirror ? "AN" : "AUS"); break;
    case M_ALT:
      if (screen == Screen::Edit) snprintf(buf, n, "%d", (int)lroundf(editAltitude));
      else if (!isnan(airPa)) snprintf(buf, n, "%d", (int)lroundf(altitudeFromPressure(airPa, settings.qnhPa)));
      else snprintf(buf, n, "--");
      setUnit("M");
      break;
    case M_HOUR: snprintf(buf, n, "%02d", rtcField(true)); break;
    case M_MIN: snprintf(buf, n, "%02d", rtcField(false)); break;
    case M_WB: snprintf(buf, n, "%u", (unsigned)settings.wheelbaseCm), setUnit("CM"); break;
    case M_TRACK: snprintf(buf, n, "%u", (unsigned)settings.trackCm), setUnit("CM"); break;
    case M_BRIGHT: snprintf(buf, n, "%u", (unsigned)(settings.contrast * 100 / 255)), setUnit("%"); break;
    case M_VOL: snprintf(buf, n, "%u", (unsigned)(settings.volume * 100 / 255)), setUnit("%"); break;
    case M_SOUND: snprintf(buf, n, "%s", settings.buzzer ? "AN" : "AUS"); break;
    case M_DEMO: snprintf(buf, n, "%s", settings.demo ? "AN" : "AUS"); break;
    default: buf[0] = 0;
  }
}

static int addClamped(int v, int d, int lo, int hi) { return v + d < lo ? lo : (v + d > hi ? hi : v + d); }

static void setRtc(int hour, int minute) {
  if (!rtcOk) return;
  m5::rtc_time_t t(hour, minute, 0);
  M5.Rtc.setTime(&t);
}

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
    case M_MAGOFF: settings.compassOffsetDeg = addClamped(settings.compassOffsetDeg, d, -180, 180); break;
    case M_ALT: editAltitude = addClamped((int)lroundf(editAltitude), d * 10, -400, 5000); break;
    case M_HOUR: setRtc((rtcField(true) + d + 2400) % 24, rtcField(false)); break;
    case M_MIN: setRtc(rtcField(true), (rtcField(false) + d + 6000) % 60); break;
    case M_WB: settings.wheelbaseCm = addClamped(settings.wheelbaseCm, d, 150, 600); break;
    case M_TRACK: settings.trackCm = addClamped(settings.trackCm, d, 100, 250); break;
    case M_BRIGHT: settings.contrast = addClamped(settings.contrast, d * 13, 10, 255); break;
    case M_VOL: settings.volume = addClamped(settings.volume, d * 13, 0, 255); break;
  }
  applySettings();
}

static void drawMenu() {
  char pos[8];
  snprintf(pos, sizeof(pos), "%d/%d", menuIdx + 1, (int)M_COUNT);
  header("MENUE", pos);
  if (menuIdx < menuTop) menuTop = menuIdx;
  if (menuIdx >= menuTop + MENU_ROWS) menuTop = menuIdx - MENU_ROWS + 1;
  for (int r = 0; r < MENU_ROWS && menuTop + r < M_COUNT; ++r) {
    int i = menuTop + r, y = 15 + r * 24;
    uint16_t col = AMB;
    if (i == menuIdx) {
      canvas.fillRect(0, y - 2, W, 22, AMB);
      col = BG;
    }
    ptext(MENU_LABELS[i], 4, y + 1, 2, col);
    char val[12];
    menuValue(i, val, sizeof(val));
    ptext(val, 236, y + 1, 2, col, AL_R);
  }
}

static void drawEdit() {
  char val[12], unit[12];
  menuValue(menuIdx, val, sizeof(val), unit, sizeof(unit));
  header(EDIT_TITLES[menuIdx], unit);
  seg(val, W / 2, 28, 64, AMB, GHOST, AL_C);
  ptext("DREHEN = WERT   DRUECKEN = OK", W / 2, 124, 1, MID, AL_C);
}

// ============================================================ Anlernen / Kalibrieren
static int pairWheel = 0;
static bool pairHasCandidate = false;
static SensorReading pairCandidate;
static const char* const WHEEL_LONG[4] = {"VORNE LINKS", "VORNE RECHTS", "HINTEN LINKS", "HINTEN RECHTS"};

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

static void drawPair() {
  char buf[40];
  snprintf(buf, sizeof(buf), "%d/4", pairWheel + 1);
  header("SENSOREN ANLERNEN", buf);
  ptext(wheelName((Wheel)pairWheel), 4, 20, 4, AMB);
  if (pairHasCandidate) {
    char id[12];
    formatShortId(pairCandidate.id, id, sizeof(id));
    ptext("GEFUNDEN:", 70, 22, 1, MID);
    snprintf(buf, sizeof(buf), "%s %s", sensorTypeName(pairCandidate.type), id);
    ptext(buf, 70, 34, 1, AMB);
    snprintf(buf, sizeof(buf), "%.2f BAR  %d DBM", pairCandidate.pressureBar, pairCandidate.rssi);
    ptext(buf, 70, 48, 1, AMB);
    ptext("KURZ = UEBERNEHMEN   LANG = ABBRUCH", W / 2, 124, 1, AMB, AL_C);
  } else {
    if (blinkOn()) ptext("SUCHE ...", 70, 34, 1, AMB);
    ptext("SENSOR AUF DAS RAD", 4, 78, 2, MID);
    snprintf(buf, sizeof(buf), "%s SCHRAUBEN", WHEEL_LONG[pairWheel]);
    ptext(buf, 4, 98, 1, MID);
    ptext("KURZ = UEBERSPRINGEN   LANG = ABBRUCH", W / 2, 124, 1, MID, AL_C);
  }
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

static void drawCalib() {
  header("NEIGUNG NULLEN");
  ptext("FAHRZEUG EBEN ABSTELLEN", W / 2, 22, 1, MID, AL_C);
  char buf[12];
  snprintf(buf, sizeof(buf), "%.1f", rollDeg);
  seg(buf, 60, 44, 40, AMB, GHOST, AL_C);
  snprintf(buf, sizeof(buf), "%.1f", pitchDeg);
  seg(buf, 180, 44, 40, AMB, GHOST, AL_C);
  ptext("SEITE", 60, 92, 1, MID, AL_C);
  ptext("LAENGS", 180, 92, 1, MID, AL_C);
  ptext("KURZ = SPEICHERN   LANG = ABBRUCH", W / 2, 124, 1, AMB, AL_C);
}

static void drawMagCalib() {
  header("KOMPASS KALIBRIEREN");
  ptext("LANGSAM EINEN KREIS FAHREN", W / 2, 22, 1, MID, AL_C);
  ptext("(ODER 2 RUNDEN AUF PARKPLATZ)", W / 2, 34, 1, MID, AL_C);
  int pct = (int)lroundf(magCal.coverage() * 100);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", pct);
  seg(buf, 110, 50, 44, AMB, GHOST, AL_R);
  ptext("%", 116, 74, 2, AMB);
  canvas.drawRect(150, 60, 80, 14, MID);
  canvas.fillRect(152, 62, 76 * pct / 100, 10, AMB);
  ptext(magCal.usable() && pct >= 75 ? "KURZ = FERTIG   LANG = ABBRUCH" : "WEITER DREHEN ...   LANG = ABBRUCH", W / 2,
        124, 1, AMB, AL_C);
}

static void drawOverlay() {
  if (!overlayText || millis() >= overlayUntil) return;
  canvas.fillRect(20, 40, W - 40, 50, AMB);
  ptext(overlayText, W / 2, 53, 3, BG, AL_C);
}

// ============================================================ Sensoren
static bool bmpBegin() {
  const uint8_t addrs[2] = {0x76, 0x77};
  for (uint8_t a : addrs) {
    uint8_t id = M5.Ex_I2C.readRegister8(a, 0xD0, 400000);
    if (id != 0x58 && id != 0x60) continue;  // BMP280 / BME280
    uint8_t raw[24];
    if (!M5.Ex_I2C.readRegister(a, 0x88, raw, 24, 400000)) continue;
    bmp280ParseCalib(raw, bmpCal);
    M5.Ex_I2C.writeRegister8(a, 0xF5, 0x90, 400000);  // 500 ms Standby, Filter x16
    M5.Ex_I2C.writeRegister8(a, 0xF4, 0x57, 400000);  // Temp x2, Druck x16, Dauerbetrieb
    bmpAddr = a;
    return true;
  }
  return false;
}

static void updateAir(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 1000) return;
  last = now;
  if (settings.demo) {
    float t = now / 1000.0f;
    airPa = 95600.0f - fmodf(t, 3600.0f) * 0.05f;  // ca. 480 m, langsam fallend
    airTempC = 22.0f + sinf(t / 300.0f);
  } else if (bmpAddr) {
    uint8_t d[6];
    if (!M5.Ex_I2C.readRegister(bmpAddr, 0xF7, d, 6, 400000)) return;
    int32_t adcP = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    int32_t adcT = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
    float t, p;
    if (!bmp280Compensate(bmpCal, adcT, adcP, t, p)) return;
    airTempC = t;
    airPa = isnan(airPa) ? p : airPa + 0.2f * (p - airPa);
  } else {
    return;
  }
  trend.add(airPa, now);
}

static void updateMotion() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 20) return;  // 50 Hz
  last = now;

  if (settings.demo) {
    float t = now / 1000.0f;
    float r = 6.0f * sinf(t / 7.0f) * 0.0174533f, p = 4.0f * sinf(t / 11.0f) * 0.0174533f;
    Vec3 a = {sinf(p), sinf(r) * cosf(p), cosf(r) * cosf(p)};
    TiltCalc flat;
    flat.setup({0, 0, 1}, 0);
    flat.compute(a, rollDeg, pitchDeg);
    headingDeg = fmodf(t * 3.0f, 360.0f);
    return;
  }
  if (!imuOk) return;
  M5.Imu.update();
  Vec3 a;
  if (!M5.Imu.getAccel(&a.x, &a.y, &a.z)) return;
  const float k = 0.02f;  // Tiefpass, Zeitkonstante ca. 1 s
  accelFilt.x += k * (a.x - accelFilt.x);
  accelFilt.y += k * (a.y - accelFilt.y);
  accelFilt.z += k * (a.z - accelFilt.z);
  tilt.compute(accelFilt, rollDeg, pitchDeg);

  Vec3 m;
  if (M5.Imu.getMag(&m.x, &m.y, &m.z) && (fabsf(m.x) + fabsf(m.y) + fabsf(m.z)) > 1.0f) {
    magOk = true;
    magRaw = m;
    if (screen == Screen::MagCalib) magCal.add(m);
    Vec3 mc = settings.magCalibrated ? Vec3{m.x - settings.magOffset[0], m.y - settings.magOffset[1],
                                            m.z - settings.magOffset[2]}
                                     : (screen == Screen::MagCalib ? Vec3{m.x - magCal.offset().x,
                                                                          m.y - magCal.offset().y,
                                                                          m.z - magCal.offset().z}
                                                                   : m);
    float h = tilt.heading(mc, accelFilt);
    if (screen == Screen::MagCalib) magCal.addHeadingSample(h);
    h = applyCompassCorrection(h, settings.compassOffsetDeg, settings.compassMirror);
    // Glaetten ueber Sinus/Kosinus (sauber ueber 0/360 hinweg)
    headSin += 0.05f * (sinf(h * 0.0174533f) - headSin);
    headCos += 0.05f * (cosf(h * 0.0174533f) - headCos);
    headingDeg = atan2f(headSin, headCos) * 57.29578f;
    if (headingDeg < 0) headingDeg += 360.0f;
  }
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

// ============================================================ Eingabe
static int readDetents() {
  static long consumed = 0;
  long raw = DinMeter.Encoder.read();
  long steps = (raw - consumed) / ENC_COUNTS_PER_DETENT;
  consumed += steps * ENC_COUNTS_PER_DETENT;
  return (int)steps;
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
      else showOverlay("KEIN SENSOR");
      break;
    case M_MAGCAL:
      if (magOk) {
        magCal.reset();
        screen = Screen::MagCalib;
      } else {
        showOverlay("KEIN SENSOR");
      }
      break;
    case M_MAGMIR:
      settings.compassMirror = !settings.compassMirror;
      saveSettings();
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
    case M_ALT:
      if (isnan(airPa)) {
        showOverlay("KEIN SENSOR");
        break;
      }
      editAltitude = altitudeFromPressure(airPa, settings.qnhPa);
      screen = Screen::Edit;
      break;
    case M_HOUR:
    case M_MIN:
      if (!rtcOk) {
        showOverlay("KEINE UHR");
        break;
      }
      screen = Screen::Edit;
      break;
    default: screen = Screen::Edit;
  }
}

static void handleInput() {
  int steps = readDetents();
  bool shortPress = M5.BtnA.wasClicked();
  bool longPress = M5.BtnA.wasHold();
  uint32_t now = millis();
  if (steps || shortPress || longPress) lastInputMs = now;

  switch (screen) {
    case Screen::Main:
      if (steps) {
        page = ((page + steps) % PAGE_COUNT + PAGE_COUNT) % PAGE_COUNT;
        click();
      }
      if (shortPress) {
        if (alarms.hasUnacknowledged()) {
          alarms.acknowledgeAll();
          beeper.stop();
        } else {
          settings.mode = currentMode() == ROAD ? OFFROAD : ROAD;
          saveSettings();
          showOverlay(modeName(currentMode()));
          click();
        }
      } else if (longPress) {
        screen = Screen::Menu;
        menuIdx = menuTop = 0;
        click();
      }
      break;

    case Screen::Menu:
      if (steps) menuIdx = addClamped(menuIdx, steps, 0, M_COUNT - 1), click();
      if (shortPress) activateMenuItem();
      else if (longPress || now - lastInputMs > MENU_TIMEOUT_MS) screen = Screen::Main;
      break;

    case Screen::Edit:
      if (steps) menuAdjust(menuIdx, steps);
      if (shortPress || longPress || now - lastInputMs > MENU_TIMEOUT_MS) {
        if (menuIdx == M_ALT && shortPress && !isnan(airPa)) settings.qnhPa = qnhFromAltitude(airPa, editAltitude);
        clampSettings(settings);
        applySettings();
        saveSettings();
        click();
        screen = Screen::Menu;
      }
      break;

    case Screen::Pair:
      if (shortPress) pairingNext(true);
      else if (longPress) {
        saveSensors();
        screen = Screen::Main;
      }
      break;

    case Screen::Calib:
      if (shortPress) {
        calibrateLevel();
        beeper.play(BEEP_OK, 4, false, 4000);
        showOverlay("GESPEICHERT");
        screen = Screen::Main;
        page = P_LEVEL;
      } else if (longPress) {
        screen = Screen::Menu;
      }
      break;

    case Screen::MagCalib:
      if (shortPress && magCal.usable() && magCal.coverage() >= 0.75f) {
        Vec3 o = magCal.offset();
        settings.magOffset[0] = o.x;
        settings.magOffset[1] = o.y;
        settings.magOffset[2] = o.z;
        settings.magCalibrated = true;
        saveSettings();
        beeper.play(BEEP_OK, 4, false, 4000);
        showOverlay("GESPEICHERT");
        screen = Screen::Main;
        page = P_COMPASS;
      } else if (longPress) {
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

  if (haveTilt()) {
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
  cfg.external_imu = true;  // Unit Mini IMU-Pro an Port A suchen
  DinMeter.begin(cfg, true /* Drehknopf */);
  M5.BtnA.setHoldThresh(800);
  M5.Display.setRotation(1);  // quer
  Serial.begin(115200);

  loadAll();
  applySettings();
  imuOk = M5.Imu.isEnabled();
  rtcOk = M5.Rtc.isEnabled();
  if (imuOk) bmpBegin();
  accelFilt = {settings.level[0], settings.level[1], settings.level[2]};

  canvas.setColorDepth(16);
  canvas.createSprite(W, H);
  canvas.fillScreen(BG);
  ptext("SPRINTER", W / 2, 30, 3, AMB, AL_C);
  ptext("BORDINSTRUMENT", W / 2, 62, 2, MID, AL_C);
  char info[40];
  snprintf(info, sizeof(info), "LAGE:%s  LUFTDRUCK:%s", imuOk ? "JA" : "NEIN", bmpAddr ? "JA" : "NEIN");
  ptext(info, W / 2, 100, 1, MID, AL_C);
  canvas.pushSprite(0, 0);

  blet::begin();
  Serial.printf("Start. Lage: %d  Luftdruck: %d  Uhr: %d\n", imuOk, bmpAddr != 0, rtcOk);
}

void loop() {
  DinMeter.update();
  uint32_t now = millis();
  handleInput();
  processReadings(now);
  if (settings.demo) demoTick(now);
  updateMotion();
  updateAir(now);

  static uint32_t lastEval = 0, lastDraw = 0, lastKeep = 0;
  if (now - lastEval >= 250) {
    lastEval = now;
    evaluateAll(now);
  }
  if (now - lastKeep >= 10000) {
    lastKeep = now;
    blet::keepAlive();
    Serial.printf("[%s] roll %.1f pitch %.1f kurs %.0f p %.0f |", modeName(currentMode()), rollDeg, pitchDeg,
                  headingDeg, airPa);
    for (int w = 0; w < WHEEL_COUNT; ++w)
      Serial.printf(" %s %.2f %s", wheelName((Wheel)w), monitors[w].pressure(), stateText(states[w]));
    Serial.println();
  }
  beeper.update();

  if (now - lastDraw >= 50) {  // ~20 fps
    lastDraw = now;
    canvas.fillScreen(BG);
    switch (screen) {
      case Screen::Main:
        switch (page) {
          case P_TIRES: drawTiresPage(); break;
          case P_TEMPS: drawTempsPage(); break;
          case P_TILT: drawTiltPage(); break;
          case P_COMPASS: drawCompassPage(); break;
          case P_BORD: drawBordPage(); break;
          default: drawLevelPage(); break;
        }
        break;
      case Screen::Menu: drawMenu(); break;
      case Screen::Edit: drawEdit(); break;
      case Screen::Pair: drawPair(); break;
      case Screen::Calib: drawCalib(); break;
      case Screen::MagCalib: drawMagCalib(); break;
    }
    drawOverlay();
    canvas.pushSprite(0, 0);
  }
}
