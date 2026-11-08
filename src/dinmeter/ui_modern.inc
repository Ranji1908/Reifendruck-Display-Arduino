// Design "Modern" (V2) – wird von main.cpp eingebunden und nutzt deren Zustand.
// Gleiche Werte und Seiten wie das Retro-Design, aber: dunkle Karten mit runden Ecken,
// kantengeglaettete Schrift, Statusfarben gruen/orange/rot, Akzentfarbe Tuerkis.
// Vorschau: docs/bilder/v2_modern/uebersicht.png (Generator: tools/vorschau_modern.py)
//
// Hinweis: Die Schriften enthalten nur ASCII – daher "Hoehe", "Strasse" und ein selbst
// gezeichnetes Grad-Zeichen.

namespace modern {

static const uint16_t M_BG = 0x0841;
static const uint16_t M_CARD = 0x18E3;
static const uint16_t M_LINE = 0x31A6;
static const uint16_t M_TX = 0xFFFF;
static const uint16_t M_SUB = 0x8C92;
static const uint16_t M_OK = 0x3666;
static const uint16_t M_WARN = 0xFD00;
static const uint16_t M_CRIT = 0xFA28;
static const uint16_t M_ROAD = 0x041F;
static const uint16_t M_ACC = 0x2E7B;

enum Sz : uint8_t { S9, S12, S18, S24, S40 };

static const lgfx::IFont* font(Sz s) {
  switch (s) {
    case S9: return &fonts::DejaVu9;
    case S12: return &fonts::DejaVu12;
    case S18: return &fonts::DejaVu18;
    case S24: return &fonts::FreeSansBold9pt7b;
    default: return &fonts::FreeSansBold18pt7b;
  }
}
static int px(Sz s) {  // Nenn-Groesse fuer das Grad-Zeichen
  static const int v[] = {9, 12, 18, 24, 40};
  return v[s];
}

static void txt(const char* str, int x, int y, Sz s, uint16_t col, uint8_t datum = middle_left) {
  canvas.setFont(font(s));
  canvas.setTextSize(1);
  canvas.setTextDatum(datum);
  canvas.setTextColor(col);
  canvas.drawString(str, x, y);
}
static int tw(const char* str, Sz s) {
  canvas.setFont(font(s));
  return canvas.textWidth(str);
}
static void degSign(int x, int y, Sz s, uint16_t col) {
  int n = px(s);
  int r = n * 9 / 100 < 2 ? 2 : n * 9 / 100;
  canvas.drawCircle(x + r + 1, y - n * 22 / 100, r, col);
  if (n >= 24) canvas.drawCircle(x + r + 1, y - n * 22 / 100, r - 1, col);
}

static uint16_t sevCol(uint8_t sev) { return sev >= 3 ? M_CRIT : (sev >= 1 ? M_WARN : M_OK); }

static void titleBar(const char* title, const char* right = nullptr) {
  txt(title, 5, 10, S12, M_TX);
  if (right) txt(right, 236, 10, S12, M_SUB, middle_right);
}

static void modePill() {
  const bool road = currentMode() == ROAD;
  const char* t = road ? "Strasse" : "Offroad";
  int w = tw(t, S12) + 12;
  canvas.fillSmoothRoundRect(4, 3, w, 14, 7, road ? M_ROAD : M_WARN);
  txt(t, 10, 10, S12, road ? M_TX : M_BG);
}

static void statusRight(const char* t, uint16_t col) {
  int w = tw(t, S12);
  canvas.fillSmoothCircle(236 - w - 7, 10, 3, col);
  txt(t, 236, 10, S12, col, middle_right);
}

// Fahrzeug quer (Front rechts) mit Radfarben VL,VR,HL,HR
static void van(int cx, int cy, const uint16_t cols[4]) {
  canvas.fillSmoothRoundRect(cx - 20, cy - 11, 40, 22, 5, M_LINE);
  canvas.fillSmoothRoundRect(cx + 6, cy - 9, 10, 18, 3, M_CARD);
  const int x[4] = {cx + 8, cx + 8, cx - 16, cx - 16}, y[4] = {cy - 14, cy + 11, cy - 14, cy + 11};
  for (int i = 0; i < 4; ++i) canvas.fillSmoothRoundRect(x[i], y[i], 9, 4, 1, cols[i]);
}

static const int CX[4] = {144, 144, 4, 4}, CY[4] = {20, 78, 20, 78};  // Karten VL,VR,HL,HR
static const int CW = 92, CH = 55;

static void card(int i, const char* big, const char* unit, bool degree, const char* foot, uint16_t dot, bool alarm,
                 float ratio = -1) {
  const int x = CX[i], y = CY[i];
  canvas.fillSmoothRoundRect(x, y, CW, CH, 8, M_CARD);
  if (alarm) {
    canvas.drawRoundRect(x, y, CW, CH, 8, M_CRIT);
    canvas.drawRoundRect(x + 1, y + 1, CW - 2, CH - 2, 7, M_CRIT);
  }
  txt(wheelName((Wheel)i), x + 8, y + 10, S12, M_SUB);
  canvas.fillSmoothCircle(x + CW - 10, y + 10, 3, dot);
  uint16_t vc = alarm ? M_CRIT : M_TX;
  txt(big, x + 8, y + 31, S24, vc);
  int bw = tw(big, S24);
  if (degree) degSign(x + 8 + bw, y + 31, S24, vc);
  else if (unit) txt(unit, x + 11 + bw, y + 35, S9, M_SUB);
  txt(foot, x + 8, y + CH - 12, S9, alarm ? M_CRIT : M_SUB);
  if (ratio >= 0) {  // Balken: Druck relativ zum Soll, Strich = Soll
    const int bx = x + 50, by = y + CH - 13, bl = CW - 58;
    canvas.fillSmoothRoundRect(bx, by, bl, 3, 1, M_LINE);
    float f = ratio / 1.25f;
    f = f < 0 ? 0 : (f > 1 ? 1 : f);
    int fl = (int)(bl * f) < 3 ? 3 : (int)(bl * f);
    canvas.fillSmoothRoundRect(bx, by, fl, 3, 1, dot);
    canvas.fillRect(bx + (int)(bl / 1.25f), by - 2, 1, 7, M_TX);
  }
}

static void fillPoly(float cx, float cy, const int8_t (*p)[2], int n, float deg, float k, uint16_t col) {
  float s = sinf(deg * 0.0174533f), c = cosf(deg * 0.0174533f);
  int xs[8], ys[8];
  for (int i = 0; i < n && i < 8; ++i) {
    xs[i] = (int)lroundf(cx + k * (p[i][0] * c - p[i][1] * s));
    ys[i] = (int)lroundf(cy + k * (p[i][0] * s + p[i][1] * c));
  }
  for (int i = 1; i + 1 < n; ++i) canvas.fillTriangle(xs[0], ys[0], xs[i], ys[i], xs[i + 1], ys[i + 1], col);
}

static void noSensor(const char* title, const char* what) {
  titleBar(title);
  canvas.fillSmoothRoundRect(4, 20, 232, 113, 8, M_CARD);
  txt(what, 120, 60, S18, M_WARN, middle_center);
  txt("Passende Unit an Port A", 120, 88, S12, M_SUB, middle_center);
  txt("stecken und neu starten", 120, 104, S12, M_SUB, middle_center);
}

// Statustexte in normaler Schreibweise
static const char* nice(State s) {
  switch (s) {
    case State::NotPaired: return "nicht angelernt";
    case State::NoData: return "kein Signal";
    case State::High: return "zu hoch";
    case State::LowWarn: return "niedrig";
    case State::LowCrit: return "kritisch";
    case State::FastLeak: return "Leck!";
    default: return "OK";
  }
}

// ---------------------------------------------------------------- Seiten
static void tires() {
  modePill();
  Wheel worst = alarms.worstWheel(states);
  uint8_t ws = worst == WHEEL_COUNT ? 0 : severity(states[worst]);
  if (ws >= 1) {
    char b[24];
    snprintf(b, sizeof(b), "%s %s", wheelName(worst), nice(states[worst]));
    if (ws < 2 || blinkOn()) statusRight(b, sevCol(ws));
  } else {
    bool any = false;
    for (int w = 0; w < WHEEL_COUNT; ++w) any |= states[w] == State::Ok;
    statusRight(any ? "Alles OK" : "keine Sensoren", any ? M_OK : M_SUB);
  }
  uint16_t cols[4];
  for (int w = 0; w < WHEEL_COUNT; ++w) {
    State s = states[w];
    uint8_t sev = severity(s);
    bool noVal = s == State::NotPaired || s == State::NoData || !monitors[w].hasData();
    cols[w] = noVal ? M_SUB : sevCol(sev);
    char big[8], foot[24];
    if (noVal) strcpy(big, "--");
    else snprintf(big, sizeof(big), "%.1f", monitors[w].pressure());
    float tgt = targetFor(settings, currentMode(), (Wheel)w);
    if (sev > 0 || noVal) snprintf(foot, sizeof(foot), "%s", nice(s));
    else if (monitors[w].battery() < 20) snprintf(foot, sizeof(foot), "Batterie %u%%", (unsigned)monitors[w].battery());
    else snprintf(foot, sizeof(foot), "Soll %.1f", tgt);
    bool alarm = sev >= 2 && blinkOn();
    card(w, big, "bar", false, foot, cols[w], alarm, noVal ? -1 : monitors[w].pressure() / tgt);
  }
  van(120, 77, cols);
}

static void temps() {
  titleBar("Reifentemperatur");
  const uint16_t grey[4] = {M_LINE, M_LINE, M_LINE, M_LINE};
  for (int w = 0; w < WHEEL_COUNT; ++w) {
    bool noVal = states[w] == State::NotPaired || states[w] == State::NoData || !monitors[w].hasData();
    uint8_t b = monitors[w].battery();
    char big[8], foot[24];
    if (noVal) strcpy(big, "--");
    else snprintf(big, sizeof(big), "%d", (int)lroundf(monitors[w].temperature()));
    if (noVal) snprintf(foot, sizeof(foot), "%s", nice(states[w]));
    else snprintf(foot, sizeof(foot), "Batterie %u%%", (unsigned)b);
    uint16_t bc = b < 20 ? M_WARN : M_OK;
    card(w, big, nullptr, !noVal, foot, noVal ? M_SUB : bc, false);
    if (!noVal) {  // Batterie-Symbol
      const int x = CX[w] + CW - 26, y = CY[w] + CH - 16;
      canvas.drawRoundRect(x, y, 18, 8, 2, M_SUB);
      canvas.fillRect(x + 18, y + 2, 2, 4, M_SUB);
      canvas.fillRect(x + 2, y + 2, 14 * b / 100 < 1 ? 1 : 14 * b / 100, 4, bc);
    }
  }
  van(120, 77, grey);
}

static void tiltPage() {
  if (!haveTilt()) return noSensor("Neigung", "Kein Lagesensor");
  titleBar("Neigung");
  static const int8_t rearBody[4][2] = {{-22, -20}, {22, -20}, {24, 14}, {-24, 14}};
  static const int8_t rearWin[4][2] = {{-16, -15}, {16, -15}, {16, -6}, {-16, -6}};
  static const int8_t rearWl[4][2] = {{-21, 14}, {-11, 14}, {-11, 21}, {-21, 21}};
  static const int8_t rearWr[4][2] = {{11, 14}, {21, 14}, {21, 21}, {11, 21}};
  static const int8_t sideBody[6][2] = {{-38, -22}, {16, -22}, {28, -9}, {38, -5}, {38, 14}, {-38, 14}};
  static const int8_t sideWin[3][2] = {{17, -18}, {25, -9}, {17, -9}};
  static const int8_t sideWr[4][2] = {{-30, 14}, {-19, 14}, {-19, 21}, {-30, 21}};
  static const int8_t sideWf[4][2] = {{20, 14}, {31, 14}, {31, 21}, {20, 21}};

  for (int i = 0; i < 2; ++i) {
    const int x = i == 0 ? 4 : 122, cx = i == 0 ? 61 : 179;
    const float v = i == 0 ? rollDeg : pitchDeg;
    const float warnAt = i == 0 ? settings.tiltWarnRoll : settings.tiltWarnPitch;
    const uint16_t col = fabsf(v) >= warnAt ? M_CRIT : (fabsf(v) >= warnAt * 0.7f ? M_WARN : M_ACC);
    canvas.fillSmoothRoundRect(x, 20, 114, 113, 8, M_CARD);
    canvas.drawFastHLine(x + 10, 62, 94, M_LINE);
    if (i == 0) {
      fillPoly(cx, 46, rearBody, 4, rollDeg, 0.8f, col);
      fillPoly(cx, 46, rearWin, 4, rollDeg, 0.8f, M_CARD);
      fillPoly(cx, 46, rearWl, 4, rollDeg, 0.8f, M_SUB);
      fillPoly(cx, 46, rearWr, 4, rollDeg, 0.8f, M_SUB);
    } else {
      fillPoly(cx, 46, sideBody, 6, -pitchDeg, 0.8f, col);
      fillPoly(cx, 46, sideWin, 3, -pitchDeg, 0.8f, M_CARD);
      fillPoly(cx, 46, sideWr, 4, -pitchDeg, 0.8f, M_SUB);
      fillPoly(cx, 46, sideWf, 4, -pitchDeg, 0.8f, M_SUB);
    }
    char b[8];
    snprintf(b, sizeof(b), "%.0f", fabsf(v));
    int w = tw(b, S40);
    uint16_t vc = (col == M_CRIT && !blinkOn()) ? M_SUB : M_TX;
    txt(b, cx - w / 2 - 4, 96, S40, vc);
    degSign(cx + w / 2 - 4, 96, S40, vc);
    const char* cap = i == 0 ? (fabsf(v) < 1 ? "Seite" : (v > 0 ? "Seite - rechts tief" : "Seite - links tief"))
                             : (fabsf(v) < 1 ? "Laengs" : (v > 0 ? "Laengs - bergauf" : "Laengs - bergab"));
    txt(cap, cx, 124, S9, M_SUB, middle_center);
  }
}

static void compassPage() {
  if (!haveMag()) return noSensor("Kompass", "Kein Magnetsensor");
  titleBar("Kompass", settings.magCalibrated || settings.demo ? nullptr : "bitte kalibrieren");
  canvas.fillSmoothRoundRect(4, 20, 232, 113, 8, M_CARD);
  const int cx = 62, cy = 77, r = 48;
  const float h = headingDeg;
  canvas.drawCircle(cx, cy, r, M_LINE);
  canvas.drawCircle(cx, cy, r - 1, M_LINE);
  for (int d = 0; d < 360; d += 15) {  // Rose dreht sich, Fahrtrichtung bleibt oben
    float a = (d - h - 90) * 0.0174533f;
    int r0 = r - (d % 45 == 0 ? 7 : 4);
    canvas.drawLine(cx + r0 * cosf(a), cy + r0 * sinf(a), cx + (r - 1) * cosf(a), cy + (r - 1) * sinf(a),
                    d % 90 == 0 ? M_TX : M_SUB);
  }
  static const char* const L[4] = {"N", "O", "S", "W"};
  for (int i = 0; i < 4; ++i) {
    float a = (i * 90 - h - 90) * 0.0174533f;
    txt(L[i], cx + (int)lroundf((r - 15) * cosf(a)), cy + (int)lroundf((r - 15) * sinf(a)), S12, i == 0 ? M_CRIT : M_TX,
        middle_center);
  }
  canvas.fillTriangle(cx, cy - r - 3, cx - 5, cy - r + 7, cx + 5, cy - r + 7, M_ACC);
  canvas.fillTriangle(cx, cy - 20, cx - 5, cy, cx + 5, cy, M_ACC);
  canvas.fillTriangle(cx - 5, cy, cx, cy + 3, cx + 5, cy, M_ACC);

  int hd = ((int)lroundf(h)) % 360;
  char b[8];
  snprintf(b, sizeof(b), "%d", hd);
  txt(b, 128, 70, S40, M_TX);
  degSign(128 + tw(b, S40), 70, S40, M_TX);
  static const char* const LONG[8] = {"Nord", "Nordost", "Ost", "Suedost", "Sued", "Suedwest", "West", "Nordwest"};
  const char* cp = compassPoint((float)hd);
  txt(cp, 130, 104, S24, M_ACC);
  txt(LONG[(int)((hd + 22.5f) / 45.0f) & 7], 130 + tw(cp, S24) + 8, 105, S12, M_SUB);
}

static void bordPage() {
  char clock[8] = "--:--";
  m5::rtc_time_t t;
  if (rtcOk && M5.Rtc.getTime(&t) && t.hours >= 0) snprintf(clock, sizeof(clock), "%02d:%02d", t.hours, t.minutes);
  titleBar("Bord", clock);
  char alt[12] = "--", hpa[12] = "--", tmp[12] = "--";
  if (haveAir() && !isnan(airPa)) {
    snprintf(alt, sizeof(alt), "%d", (int)lroundf(altitudeFromPressure(airPa, settings.qnhPa)));
    snprintf(hpa, sizeof(hpa), "%d", (int)lroundf(airPa / 100.0f));
  }
  if (haveAir() && !isnan(airTempC)) snprintf(tmp, sizeof(tmp), "%d", (int)lroundf(airTempC));
  struct Tile {
    int x, y;
    const char *lab, *val, *unit;
  } tiles[4] = {{4, 20, "Hoehe", alt, "m"}, {122, 20, "Uhrzeit", clock, ""}, {4, 78, "Luftdruck", hpa, "hPa"},
                {122, 78, "Innen", tmp, "C"}};
  for (auto& tl : tiles) {
    canvas.fillSmoothRoundRect(tl.x, tl.y, 114, 55, 8, M_CARD);
    txt(tl.lab, tl.x + 8, tl.y + 10, S12, M_SUB);
    txt(tl.val, tl.x + 8, tl.y + 33, S24, M_TX);
    if (tl.unit[0]) txt(tl.unit, tl.x + 11 + tw(tl.val, S24), tl.y + 37, S12, M_SUB);
  }
  // Trendpfeil im Luftdruck-Feld
  if (trend.valid()) {
    float c = trend.changeHpa3h();
    uint16_t col = c <= -1.0f ? M_WARN : (c >= 1.0f ? M_OK : M_SUB);
    if (c <= -1.0f) canvas.fillTriangle(104, 108, 96, 96, 112, 96, col);
    else if (c >= 1.0f) canvas.fillTriangle(104, 96, 96, 108, 112, 108, col);
    else canvas.fillTriangle(98, 96, 98, 108, 110, 102, col);
    char lt[12];
    snprintf(lt, sizeof(lt), "%s", trend.text());
    for (char* p = lt + 1; *p; ++p)
      if (*p >= 'A' && *p <= 'Z') *p += 32;  // "FAELLT" -> "Faellt"
    txt(lt, 104, 122, S9, col, middle_center);
  }
}

static void levelPage() {
  if (!haveTilt()) return noSensor("Ausrichten", "Kein Lagesensor");
  bool lvl = fabsf(rollDeg) < 0.5f && fabsf(pitchDeg) < 0.5f;
  titleBar("Ausrichten", lvl ? "eben!" : "cm unterlegen");
  float lift[WHEEL_COUNT];
  levelingLifts(rollDeg, pitchDeg, settings.wheelbaseCm, settings.trackCm, lift);
  const int chx[4] = {162, 162, 4, 4}, chy[4] = {22, 104, 22, 104};
  for (int w = 0; w < WHEEL_COUNT; ++w) {
    canvas.fillSmoothRoundRect(chx[w], chy[w], 74, 28, 8, M_CARD);
    txt(wheelName((Wheel)w), chx[w] + 8, chy[w] + 14, S12, M_SUB);
    char b[8];
    if (lift[w] < 0.5f) strcpy(b, "0");
    else snprintf(b, sizeof(b), "+%d", (int)lroundf(lift[w]));
    txt(b, chx[w] + 66, chy[w] + 14, S18, lift[w] < 0.5f ? M_OK : M_TX, middle_right);
  }
  const int cx = 120, cy = 77;
  canvas.fillSmoothCircle(cx, cy, 42, M_CARD);
  canvas.drawCircle(cx, cy, 28, M_LINE);
  canvas.drawCircle(cx, cy, 10, M_LINE);
  canvas.drawFastHLine(cx - 42, cy, 84, M_LINE);
  canvas.drawFastVLine(cx, cy - 42, 84, M_LINE);
  float bx = pitchDeg * 6.0f, by = -rollDeg * 6.0f, d = sqrtf(bx * bx + by * by);
  if (d > 34) {
    bx *= 34 / d;
    by *= 34 / d;
  }
  canvas.fillSmoothCircle(cx + (int)bx, cy + (int)by, 8, lvl ? M_OK : M_WARN);
  char b[24];
  snprintf(b, sizeof(b), "%.1f / %.1f Grad", fabsf(rollDeg), fabsf(pitchDeg));
  txt(b, cx, 128, S9, M_SUB, middle_center);
}

// ---------------------------------------------------------------- Menue und Dialoge
static void niceValue(int item, char* out, size_t n) {
  char v[12], u[12];
  menuValue(item, v, sizeof(v), u, sizeof(u));
  if (!strcmp(v, "AN")) strcpy(v, "an");
  else if (!strcmp(v, "AUS")) strcpy(v, "aus");
  else if (!strcmp(v, "RETRO")) strcpy(v, "Retro");
  else if (!strcmp(v, "MODERN")) strcpy(v, "Modern");
  for (char* p = u; *p; ++p)
    if (*p >= 'A' && *p <= 'Z') *p += 32;
  if (!strcmp(u, "grad")) strcpy(u, "Grad");
  if (u[0]) snprintf(out, n, "%s %s", v, u);
  else snprintf(out, n, "%s", v);
}

static void menu() {
  char pos[12];
  snprintf(pos, sizeof(pos), "%d / %d", menuIdx + 1, (int)M_COUNT);
  titleBar("Einstellungen", pos);
  const int rows = 4;
  static int top = 0;
  if (menuIdx < top) top = menuIdx;
  if (menuIdx >= top + rows) top = menuIdx - rows + 1;
  for (int r = 0; r < rows && top + r < M_COUNT; ++r) {
    int i = top + r, y = 21 + r * 28;
    bool sel = i == menuIdx;
    if (sel) canvas.fillSmoothRoundRect(4, y, 226, 26, 7, M_ACC);
    txt(MENU_NICE[i], 12, y + 13, S12, sel ? M_BG : M_TX);
    char v[20];
    niceValue(i, v, sizeof(v));
    txt(v, 222, y + 13, S12, sel ? M_BG : M_ACC, middle_right);
  }
  canvas.fillSmoothRoundRect(234, 21, 3, 112, 1, M_LINE);
  canvas.fillSmoothRoundRect(234, 21 + 112 * top / M_COUNT, 3, 112 * rows / M_COUNT, 1, M_SUB);
}

// Wertebereich fuer den Schieberegler im Editor
static bool editRange(int item, float& v, float& lo, float& hi) {
  if (float* t = targetPtr(item)) return v = *t, lo = 0.5f, hi = 8.0f, true;
  switch (item) {
    case M_WARN: return v = settings.warnPct, lo = 3, hi = 40, true;
    case M_CRIT: return v = settings.critPct, lo = 5, hi = 60, true;
    case M_HIGH: return v = settings.highPct, lo = 5, hi = 60, true;
    case M_LEAK_BAR: return v = settings.leakDeciBar, lo = 1, hi = 20, true;
    case M_LEAK_MIN: return v = settings.leakMinutes, lo = 1, hi = 15, true;
    case M_TILT_R: return v = settings.tiltWarnRoll, lo = 5, hi = 45, true;
    case M_TILT_P: return v = settings.tiltWarnPitch, lo = 5, hi = 45, true;
    case M_MAGOFF: return v = settings.compassOffsetDeg, lo = -180, hi = 180, true;
    case M_WB: return v = settings.wheelbaseCm, lo = 150, hi = 600, true;
    case M_TRACK: return v = settings.trackCm, lo = 100, hi = 250, true;
    case M_BRIGHT: return v = settings.contrast, lo = 10, hi = 255, true;
    case M_VOL: return v = settings.volume, lo = 0, hi = 255, true;
    default: return false;
  }
}

static void edit() {
  titleBar(MENU_NICE[menuIdx]);
  canvas.fillSmoothRoundRect(4, 20, 232, 113, 8, M_CARD);
  char v[12], u[12];
  menuValue(menuIdx, v, sizeof(v), u, sizeof(u));
  for (char* p = u; *p; ++p)
    if (*p >= 'A' && *p <= 'Z') *p += 32;
  int w = tw(v, S40);
  txt(v, 120 - w / 2 - 10, 66, S40, M_TX);
  txt(u, 120 + w / 2 - 6, 72, S12, M_SUB);
  float val, lo, hi;
  if (editRange(menuIdx, val, lo, hi)) {
    const int bx = 24, by = 98, bl = 192;
    float f = (val - lo) / (hi - lo);
    f = f < 0 ? 0 : (f > 1 ? 1 : f);
    canvas.fillSmoothRoundRect(bx, by, bl, 4, 2, M_LINE);
    canvas.fillSmoothRoundRect(bx, by, (int)(bl * f) < 4 ? 4 : (int)(bl * f), 4, 2, M_ACC);
    canvas.fillSmoothCircle(bx + (int)(bl * f), by + 2, 6, M_TX);
  }
  txt("drehen: aendern - druecken: OK", 120, 121, S9, M_SUB, middle_center);
}

static void pair() {
  char b[40];
  snprintf(b, sizeof(b), "%d / 4", pairWheel + 1);
  titleBar("Sensoren anlernen", b);
  canvas.fillSmoothRoundRect(4, 20, 232, 113, 8, M_CARD);
  static const char* const LONG[4] = {"vorne links", "vorne rechts", "hinten links", "hinten rechts"};
  txt(wheelName((Wheel)pairWheel), 14, 48, S40, M_ACC);
  txt(LONG[pairWheel], 16, 78, S12, M_SUB);
  canvas.fillSmoothRoundRect(118, 30, 108, 58, 7, M_LINE);
  if (pairHasCandidate) {
    char id[12];
    formatShortId(pairCandidate.id, id, sizeof(id));
    canvas.fillSmoothCircle(128, 42, 3, M_OK);
    txt("Gefunden", 136, 42, S12, M_TX);
    snprintf(b, sizeof(b), "%s %s", sensorTypeName(pairCandidate.type), id);
    txt(b, 126, 60, S9, M_SUB);
    snprintf(b, sizeof(b), "%.2f bar  %d dBm", pairCandidate.pressureBar, pairCandidate.rssi);
    txt(b, 126, 76, S9, M_SUB);
    txt("kurz: uebernehmen - lang: Abbruch", 120, 118, S9, M_SUB, middle_center);
  } else {
    canvas.fillSmoothCircle(128, 42, 3, blinkOn() ? M_WARN : M_LINE);
    txt("Suche ...", 136, 42, S12, M_TX);
    txt("Sensor auf das", 126, 60, S9, M_SUB);
    txt("Rad schrauben", 126, 76, S9, M_SUB);
    txt("kurz: ueberspringen - lang: Abbruch", 120, 118, S9, M_SUB, middle_center);
  }
}

static void calib() {
  titleBar("Neigung nullen");
  canvas.fillSmoothRoundRect(4, 20, 232, 113, 8, M_CARD);
  txt("Fahrzeug eben abstellen", 120, 36, S12, M_SUB, middle_center);
  char b[12];
  snprintf(b, sizeof(b), "%.1f", rollDeg);
  txt(b, 66, 70, S24, M_TX, middle_center);
  snprintf(b, sizeof(b), "%.1f", pitchDeg);
  txt(b, 174, 70, S24, M_TX, middle_center);
  txt("Seite", 66, 92, S9, M_SUB, middle_center);
  txt("Laengs", 174, 92, S9, M_SUB, middle_center);
  txt("kurz: speichern - lang: Abbruch", 120, 118, S9, M_SUB, middle_center);
}

static void magCalib() {
  titleBar("Kompass kalibrieren");
  canvas.fillSmoothRoundRect(4, 20, 232, 113, 8, M_CARD);
  txt("Langsam einen Kreis fahren", 120, 36, S12, M_SUB, middle_center);
  int pct = (int)lroundf(magCal.coverage() * 100);
  char b[8];
  snprintf(b, sizeof(b), "%d%%", pct);
  txt(b, 120, 66, S24, M_TX, middle_center);
  const int bx = 24, by = 88, bl = 192;
  canvas.fillSmoothRoundRect(bx, by, bl, 4, 2, M_LINE);
  canvas.fillSmoothRoundRect(bx, by, bl * pct / 100 < 4 ? 4 : bl * pct / 100, 4, 2, M_ACC);
  bool ready = magCal.usable() && pct >= 75;
  txt(ready ? "kurz: fertig - lang: Abbruch" : "weiter drehen ... - lang: Abbruch", 120, 118, S9, ready ? M_OK : M_SUB,
      middle_center);
}

static void overlay() {
  if (!overlayText || millis() >= overlayUntil) return;
  canvas.fillSmoothRoundRect(40, 42, 160, 46, 10, M_LINE);
  char b[24];
  snprintf(b, sizeof(b), "%s", overlayText);
  for (char* p = b + 1; *p; ++p)
    if (*p >= 'A' && *p <= 'Z') *p += 32;  // "OFFROAD" -> "Offroad"
  txt(b, 120, 65, S24, M_TX, middle_center);
}

}  // namespace modern

static void drawModernFrame() {
  using namespace modern;
  canvas.fillScreen(M_BG);
  switch (screen) {
    case Screen::Main:
      switch (page) {
        case P_TIRES: tires(); break;
        case P_TEMPS: temps(); break;
        case P_TILT: tiltPage(); break;
        case P_COMPASS: compassPage(); break;
        case P_BORD: bordPage(); break;
        default: levelPage(); break;
      }
      break;
    case Screen::Menu: menu(); break;
    case Screen::Edit: edit(); break;
    case Screen::Pair: pair(); break;
    case Screen::Calib: calib(); break;
    case Screen::MagCalib: magCalib(); break;
  }
  overlay();
}
