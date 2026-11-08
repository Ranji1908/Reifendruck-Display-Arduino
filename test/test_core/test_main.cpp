// Unit-Tests fuer die Kernlogik. Ausfuehren mit:  pio test -e native
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "TpmsCore.h"

using namespace tpms;

static const uint32_t MIN = 60UL * 1000UL;

void setUp() {}
void tearDown() {}

static SensorReading reading(float bar) {
  SensorReading r;
  r.id = 1;
  r.pressureBar = bar;
  r.temperatureC = 20;
  r.batteryPct = 90;
  return r;
}

// Beispielpaket eines echten Sensors (aus dem Theengs-Decoder-Projekt)
void test_decode_known_packet() {
  const uint8_t pkt[18] = {0x00, 0x01, 0x80, 0xEA, 0xCA, 0x10, 0x8A, 0x78, 0xE3,
                           0x6D, 0x00, 0x00, 0xE6, 0x0A, 0x00, 0x00, 0x5B, 0x00};
  SensorReading r;
  TEST_ASSERT_TRUE(decodeManufacturerData(pkt, sizeof(pkt), r));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.28131f, r.pressureBar);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 27.90f, r.temperatureC);
  TEST_ASSERT_EQUAL_UINT8(91, r.batteryPct);
  TEST_ASSERT_FALSE(r.sensorAlarm);
  TEST_ASSERT_TRUE(r.id == 0x80EACA108A78ULL);
  char buf[12];
  formatShortId(r.id, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("10:8A:78", buf);
}

void test_decode_rejects_foreign_data() {
  const uint8_t apple[18] = {0x4C, 0x00, 0x02, 0x15};
  SensorReading r;
  TEST_ASSERT_FALSE(decodeManufacturerData(apple, sizeof(apple), r));
  const uint8_t shortPkt[10] = {0x00, 0x01};
  TEST_ASSERT_FALSE(decodeManufacturerData(shortPkt, sizeof(shortPkt), r));
  TEST_ASSERT_FALSE(decodeManufacturerData(nullptr, 18, r));
}

void test_states_basic() {
  EvalParams p;  // 10 % Warnung, 20 % kritisch, 20 % hoch
  WheelMonitor m;
  TEST_ASSERT_EQUAL(State::NotPaired, m.evaluate(4.0f, p, 0, false));
  TEST_ASSERT_EQUAL(State::NoData, m.evaluate(4.0f, p, 0, true));

  m.addReading(reading(4.0f), 1000);
  TEST_ASSERT_EQUAL(State::Ok, m.evaluate(4.0f, p, 1000, true));

  WheelMonitor w;
  w.addReading(reading(3.5f), 0);  // 12,5 % unter Soll
  TEST_ASSERT_EQUAL(State::LowWarn, w.evaluate(4.0f, p, 0, true));

  WheelMonitor c;
  c.addReading(reading(3.0f), 0);  // 25 % unter Soll
  TEST_ASSERT_EQUAL(State::LowCrit, c.evaluate(4.0f, p, 0, true));

  WheelMonitor h;
  h.addReading(reading(5.0f), 0);
  TEST_ASSERT_EQUAL(State::High, h.evaluate(4.0f, p, 0, true));
}

void test_timeout() {
  EvalParams p;
  WheelMonitor m;
  m.addReading(reading(4.0f), 0);
  TEST_ASSERT_EQUAL(State::Ok, m.evaluate(4.0f, p, p.timeoutMs, true));
  TEST_ASSERT_EQUAL(State::NoData, m.evaluate(4.0f, p, p.timeoutMs + 1, true));
}

void test_hysteresis() {
  EvalParams p;
  WheelMonitor m;
  m.addReading(reading(3.58f), 0);  // Schwelle 3.6
  TEST_ASSERT_EQUAL(State::LowWarn, m.evaluate(4.0f, p, 0, true));
  m.addReading(reading(3.62f), 1000);  // knapp drueber -> bleibt Warnung
  TEST_ASSERT_EQUAL(State::LowWarn, m.evaluate(4.0f, p, 1000, true));
  m.addReading(reading(3.7f), 2000);
  TEST_ASSERT_EQUAL(State::Ok, m.evaluate(4.0f, p, 2000, true));
}

void test_fast_leak() {
  EvalParams p;  // 0,3 bar in 5 min
  WheelMonitor m;
  uint32_t t = 0;
  float bar = 4.0f;
  State s = State::Ok;
  // 0,1 bar pro Minute verlieren
  for (int i = 0; i <= 4; ++i) {
    m.addReading(reading(bar), t);
    s = m.evaluate(4.0f, p, t, true);
    t += MIN;
    bar -= 0.1f;
  }
  // nach 4 Minuten: 0,4 bar Verlust, Druck 3.6 -> Leck erkannt, obwohl erst Warnschwelle
  TEST_ASSERT_EQUAL(State::FastLeak, s);
  // Druck stabilisiert sich, Fenster laeuft ab -> bleibt Leck (gelatcht), bis Soll erreicht
  m.addReading(reading(3.6f), t + 10 * MIN);
  TEST_ASSERT_EQUAL(State::FastLeak, m.evaluate(4.0f, p, t + 10 * MIN, true));
  m.addReading(reading(4.0f), t + 20 * MIN);
  TEST_ASSERT_EQUAL(State::Ok, m.evaluate(4.0f, p, t + 20 * MIN, true));
}

void test_slow_drop_is_no_leak() {
  EvalParams p;
  WheelMonitor m;
  uint32_t t = 0;
  float bar = 4.0f;
  State s = State::Ok;
  for (int i = 0; i < 30; ++i) {  // 0,02 bar/min ueber 30 min
    m.addReading(reading(bar), t);
    s = m.evaluate(4.0f, p, t, true);
    TEST_ASSERT_NOT_EQUAL(State::FastLeak, s);
    t += MIN;
    bar -= 0.02f;
  }
  TEST_ASSERT_EQUAL(State::LowWarn, s);
}

void test_alarm_manager() {
  AlarmManager a;
  State st[4] = {State::Ok, State::Ok, State::Ok, State::Ok};
  TEST_ASSERT_EQUAL(AlarmManager::Sound::None, a.update(st));

  st[2] = State::LowWarn;  // Warnung -> genau einmal piepen
  TEST_ASSERT_EQUAL(AlarmManager::Sound::WarnOnce, a.update(st));
  TEST_ASSERT_EQUAL(AlarmManager::Sound::None, a.update(st));

  st[2] = State::LowCrit;  // Verschlechterung -> Dauer-Alarm bis Quittierung
  TEST_ASSERT_EQUAL(AlarmManager::Sound::Alarm, a.update(st));
  TEST_ASSERT_EQUAL(AlarmManager::Sound::Alarm, a.update(st));
  TEST_ASSERT_TRUE(a.hasUnacknowledged());
  TEST_ASSERT_EQUAL(RL, a.worstWheel(st));
  a.acknowledgeAll();
  TEST_ASSERT_FALSE(a.hasUnacknowledged());
  TEST_ASSERT_EQUAL(AlarmManager::Sound::None, a.update(st));

  st[2] = State::Ok;  // Besserung -> wieder scharf
  a.update(st);
  st[2] = State::LowCrit;
  TEST_ASSERT_EQUAL(AlarmManager::Sound::Alarm, a.update(st));

  st[2] = State::Ok;  // "zu hoch" macht keinen Ton
  a.update(st);
  st[0] = State::High;
  TEST_ASSERT_EQUAL(AlarmManager::Sound::None, a.update(st));
}

void test_settings() {
  Settings s;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.5f, targetFor(s, ROAD, FR));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.8f, targetFor(s, OFFROAD, RL));
  s.warnPct = 30;
  s.critPct = 10;  // ungueltig: kritisch muss groesser als Warnung sein
  s.target[0][0] = 99;
  s.mode = 7;
  clampSettings(s);
  TEST_ASSERT_TRUE(s.critPct > s.warnPct);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 8.0f, s.target[0][0]);
  TEST_ASSERT_EQUAL_UINT8(ROAD, s.mode);
}

// ---- weitere Sensorformate (Testdaten aus github.com/bkbilly/tpms_ble)
static Advertisement adv(uint64_t addr, uint16_t company, const uint8_t* payload, size_t n, uint8_t* buf,
                         const char* name = "", uint16_t svc = 0) {
  buf[0] = company & 0xFF;
  buf[1] = company >> 8;
  memcpy(buf + 2, payload, n);
  Advertisement a;
  a.address = addr;
  a.mfr = buf;
  a.mfrLen = n + 2;
  a.name = name;
  if (svc) {
    a.services[0] = svc;
    a.serviceCount = 1;
  }
  return a;
}

void test_decode_type_a_via_advertisement() {
  uint8_t buf[32];
  uint8_t p[16] = {0};
  int32_t pa = 230000, t = 2450;
  memcpy(p + 6, &pa, 4);
  memcpy(p + 10, &t, 4);
  p[14] = 85;
  SensorReading r;
  TEST_ASSERT_TRUE(decodeAdvertisement(adv(0xAABBCCDDEE01ULL, 0x0100, p, 16, buf, "TPMS1_123456"), r));
  TEST_ASSERT_EQUAL(SensorType::TypeA, r.type);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.3f, r.pressureBar);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 24.5f, r.temperatureC);
  TEST_ASSERT_EQUAL_UINT8(85, r.batteryPct);
}

void test_decode_type_b() {
  uint8_t buf[32];
  const uint8_t p[5] = {22, 0x01, 0xAE, 0, 0};
  SensorReading r;
  TEST_ASSERT_TRUE(decodeAdvertisement(adv(0xAABBCCDDEE02ULL, 0x1E00, p, 5, buf, "", 0x27A5), r));
  TEST_ASSERT_EQUAL(SensorType::TypeB, r.type);
  TEST_ASSERT_FLOAT_WITHIN(0.002f, 1.965f, r.pressureBar);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, r.temperatureC);
  TEST_ASSERT_TRUE(r.id == 0xAABBCCDDEE02ULL);
  const uint8_t neg[5] = {252, 0x01, 0xAE, 0, 0};
  TEST_ASSERT_TRUE(decodeAdvertisement(adv(1, 0x1E00, neg, 5, buf, "", 0x27A5), r));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -4.0f, r.temperatureC);
}

void test_decode_type_d() {
  uint8_t buf[32];
  const uint8_t p[15] = {0xc0, 0x3e, 0x54, 0x00, 0x0a, 0x17, 0x00, 0x8d,
                         0x28, 0x11, 0x11, 0x11, 0xbe, 0x5d, 0x30};
  SensorReading r;
  TEST_ASSERT_TRUE(decodeAdvertisement(adv(0xBE5D30112233ULL, 172, p, 15, buf), r));
  TEST_ASSERT_EQUAL(SensorType::TypeD, r.type);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.95f, r.pressureBar);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 29.0f, r.temperatureC);
  TEST_ASSERT_EQUAL_UINT8(100, r.batteryPct);
}

void test_decode_type_f() {
  uint8_t buf[32];
  const uint8_t p[9] = {0xAF, 0x45, 0x00, 0xA7, 0x00, 0x2D, 0x56, 0xA8, 0xB4};
  SensorReading r;
  TEST_ASSERT_TRUE(decodeAdvertisement(adv(0x112233445566ULL, 0x0002, p, 9, buf, "TPMS", 0xA827), r));
  TEST_ASSERT_EQUAL(SensorType::TypeF, r.type);
  TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.67f, r.pressureBar);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 19.0f, r.temperatureC);
}

void test_decode_type_g_and_h() {
  uint8_t buf[32];
  SensorReading r;
  const uint8_t g[3] = {0x15, 0x01, 0xAE};
  TEST_ASSERT_TRUE(decodeAdvertisement(adv(0x1230AF010203ULL, 0x2000, g, 3, buf, "AI-8000"), r));
  TEST_ASSERT_EQUAL(SensorType::TypeG, r.type);
  TEST_ASSERT_FLOAT_WITHIN(0.002f, 1.951f, r.pressureBar);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.0f, r.temperatureC);
  const uint8_t ga[3] = {0x0F, 0x00, 0x92};  // Umgebungsluft -> 0 bar
  TEST_ASSERT_TRUE(decodeAdvertisement(adv(0x1230AF010203ULL, 0x2000, ga, 3, buf, "AI-8000"), r));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.pressureBar);

  const uint8_t h[9] = {18, 0x01, 0xB3, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  TEST_ASSERT_TRUE(decodeAdvertisement(adv(0x112233445566ULL, 0x1B04, h, 9, buf, "TC.TPMS", 0xA828), r));
  TEST_ASSERT_EQUAL(SensorType::TypeH, r.type);
  TEST_ASSERT_FLOAT_WITHIN(0.002f, 1.993f, r.pressureBar);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.0f, r.temperatureC);
}

void test_decode_ignores_other_devices() {
  uint8_t buf[32];
  const uint8_t ibeacon[23] = {0x02, 0x15};
  SensorReading r;
  TEST_ASSERT_FALSE(decodeAdvertisement(adv(0x112233445566ULL, 0x004C, ibeacon, 23, buf, "iPhone"), r));
  Advertisement empty;
  TEST_ASSERT_FALSE(decodeAdvertisement(empty, r));
}

// ---- Neigung
static const float D2R = 0.01745329f;

void test_tilt_flat_sensor() {
  TiltCalc t;
  TEST_ASSERT_TRUE(t.setup({0, 0, 1}, 0));
  float roll, pitch;
  t.compute({0, 0, 1}, roll, pitch);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0, roll);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0, pitch);
  // Front 10 Grad hoeher: "oben" kippt im Fahrzeugsystem nach vorne (+X)
  t.compute({sinf(10 * D2R), 0, cosf(10 * D2R)}, roll, pitch);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 10.0f, pitch);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, roll);
  // rechte Seite 5 Grad tiefer: "oben" kippt nach links (+Y)
  t.compute({0, sinf(5 * D2R), cosf(5 * D2R)}, roll, pitch);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 5.0f, roll);
}

void test_tilt_vertical_mount_and_rotation() {
  // Sensor senkrecht eingebaut: Sensor-Y zeigt nach oben, Sensor-X nach vorne
  TiltCalc t;
  TEST_ASSERT_TRUE(t.setup({0, 1, 0}, 0));
  float roll, pitch;
  t.compute({sinf(8 * D2R), cosf(8 * D2R), 0}, roll, pitch);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 8.0f, pitch);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, roll);
  // gleiche Lage, aber Einbaulage um 180 Grad gedreht -> Vorzeichen kehrt sich um
  TEST_ASSERT_TRUE(t.setup({0, 1, 0}, 2));
  t.compute({sinf(8 * D2R), cosf(8 * D2R), 0}, roll, pitch);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, -8.0f, pitch);
  // um 90 Grad: Laengs wird zu Quer
  TEST_ASSERT_TRUE(t.setup({0, 1, 0}, 1));
  t.compute({sinf(8 * D2R), cosf(8 * D2R), 0}, roll, pitch);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, pitch);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 8.0f, fabsf(roll));
  TEST_ASSERT_FALSE(t.setup({0, 0, 0}, 0));
}

void test_leveling_lifts() {
  float lift[4];
  levelingLifts(0, 2.0f, 367, 171, lift);  // Front 2 Grad hoeher -> hinten unterlegen
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, lift[FL]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, lift[FR]);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 12.8f, lift[RL]);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 12.8f, lift[RR]);
  levelingLifts(3.0f, 0, 367, 171, lift);  // rechts 3 Grad tiefer -> rechts unterlegen
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, lift[FL]);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 9.0f, lift[FR]);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 9.0f, lift[RR]);
}

void test_tilt_warning_hysteresis() {
  TEST_ASSERT_FALSE(tiltWarning(false, 24.0f, 0, 25, 30));
  TEST_ASSERT_TRUE(tiltWarning(false, -25.5f, 0, 25, 30));
  TEST_ASSERT_TRUE(tiltWarning(true, 23.5f, 0, 25, 30));   // noch innerhalb Hysterese
  TEST_ASSERT_FALSE(tiltWarning(true, 22.5f, 0, 25, 30));
  TEST_ASSERT_TRUE(tiltWarning(false, 0, 31.0f, 25, 30));
}

void test_settings_tilt_clamp() {
  Settings s;
  s.level[0] = s.level[1] = s.level[2] = 0;  // kaputt
  s.mountQuarter = 7;
  s.wheelbaseCm = 10;
  clampSettings(s);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, s.level[2]);
  TEST_ASSERT_EQUAL_UINT8(3, s.mountQuarter);
  TEST_ASSERT_TRUE(s.wheelbaseCm == 150);
}

// ---- Kompass
void test_compass_heading_flat() {
  TiltCalc t;
  t.setup({0, 0, 1}, 0);
  const Vec3 g = {0, 0, 1};
  // Erdfeld: horizontal nach Norden, steil nach unten (Mitteleuropa ca. 65 Grad Inklination)
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, t.heading({20, 0, -44}, g));    // Front zeigt nach Norden
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 90.0f, t.heading({0, 20, -44}, g));   // Norden links -> Kurs Ost
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 180.0f, t.heading({-20, 0, -44}, g));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 270.0f, t.heading({0, -20, -44}, g));
  TEST_ASSERT_EQUAL_STRING("O", compassPoint(90));
  TEST_ASSERT_EQUAL_STRING("N", compassPoint(350));
  TEST_ASSERT_EQUAL_STRING("NW", compassPoint(315));
}

void test_compass_tilt_compensation() {
  // Fahrzeug steht 15 Grad bergauf (Front hoch), Front zeigt nach Osten.
  TiltCalc t;
  t.setup({0, 0, 1}, 0);
  const float a = 15 * D2R;
  Vec3 g = {sinf(a), 0, cosf(a)};  // "oben" im Fahrzeugsystem
  // Erdfeld im Weltsystem (N,W,oben) = (20, 0, -44); Fahrzeug Kurs Ost:
  // Welt-Nord liegt links (+y), dazu Nickung um die y-Achse. Vertikalanteil kippt mit:
  Vec3 m = {-44 * sinf(a), 20, -44 * cosf(a)};
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 90.0f, t.heading(m, g));
}

void test_compass_correction_and_calibrator() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, applyCompassCorrection(355, 10, false));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 270.0f, applyCompassCorrection(90, 0, true));
  MagCalibrator c;
  TEST_ASSERT_FALSE(c.usable());
  // Kreisfahrt mit Stoerfeld-Offset (10, -5, 3)
  for (int i = 0; i < 36; ++i) {
    float h = i * 10 * D2R;
    c.add({10 + 25 * cosf(h), -5 + 25 * sinf(h), 3 - 44});
    c.addHeadingSample(i * 10.0f);
  }
  TEST_ASSERT_TRUE(c.usable());
  Vec3 o = c.offset();
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 10.0f, o.x);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -5.0f, o.y);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, c.coverage());
}

// ---- Luftdruck
void test_bmp280_datasheet_example() {
  // Rechenbeispiel aus dem Bosch-Datenblatt
  const uint8_t raw[24] = {0x70, 0x6B, 0x43, 0x67, 0x18, 0xFC, 0x7D, 0x8E, 0x43, 0xD6, 0xD0, 0x0B,
                           0x27, 0x0B, 0x8C, 0x00, 0xF9, 0xFF, 0x8C, 0x3C, 0xF8, 0xC6, 0x70, 0x17};
  Bmp280Calib c;
  bmp280ParseCalib(raw, c);
  TEST_ASSERT_TRUE(c.T1 == 27504 && c.T2 == 26435 && c.T3 == -1000 && c.P1 == 36477);
  TEST_ASSERT_TRUE(c.P6 == -7 && c.P9 == 6000);
  float t, p;
  TEST_ASSERT_TRUE(bmp280Compensate(c, 519888, 415148, t, p));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.08f, t);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 100653.27f, p);
}

void test_altitude() {
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, altitudeFromPressure(101325, 101325));
  TEST_ASSERT_FLOAT_WITHIN(3.0f, 1000.0f, altitudeFromPressure(89875, 101325));  // Normatmosphaere: 1000 m
  float qnh = qnhFromAltitude(95000, 540);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 540.0f, altitudeFromPressure(95000, qnh));
}

void test_pressure_trend() {
  PressureTrend tr;
  TEST_ASSERT_FALSE(tr.valid());
  uint32_t t = 0;
  for (int i = 0; i <= 18; ++i) {  // 3 h, alle 10 min, insgesamt -4 hPa
    tr.add(101300.0f - i * (400.0f / 18), t);
    t += 600000UL;
  }
  TEST_ASSERT_TRUE(tr.valid());
  TEST_ASSERT_FLOAT_WITHIN(0.05f, -4.0f, tr.changeHpa3h());
  TEST_ASSERT_EQUAL_STRING("STURM?", tr.text());
}

// ---- QMP6988: Vergleich mit dem Original-Rechenweg aus M5Stack M5Unit-ENV (MIT)
namespace qref {
// Referenz: Formeln 1:1 aus M5Unit-ENV QMP6988.cpp
typedef int32_t S32; typedef int16_t S16; typedef int64_t S64;
struct IK{S32 a0,b00,a1,a2; S64 bt1,bt2,bp1,b11,bp2,b12,b21,bp3;};
static void refCal(const uint8_t* a,IK& ik){
  S32 a0=(S32)(((a[18]<<12)|(a[19]<<4)|(a[24]&0x0f))<<12); a0=a0>>12;
  S32 b00=(S32)(((a[0]<<12)|(a[1]<<4)|((a[24]&0xf0)>>4))<<12); b00=b00>>12;
  ik.a0=a0; ik.b00=b00;
  ik.a1=3608L*(S32)(S16)((a[20]<<8)|a[21])-1731677965L;
  ik.a2=16889L*(S32)(S16)((a[22]<<8)|a[23])-87619360L;
  ik.bt1=2982L*(S64)(S16)((a[2]<<8)|a[3])+107370906L;
  ik.bt2=329854L*(S64)(S16)((a[4]<<8)|a[5])+108083093L;
  ik.bp1=19923L*(S64)(S16)((a[6]<<8)|a[7])+1133836764L;
  ik.b11=2406L*(S64)(S16)((a[8]<<8)|a[9])+118215883L;
  ik.bp2=3079L*(S64)(S16)((a[10]<<8)|a[11])-181579595L;
  ik.b12=6846L*(S64)(S16)((a[12]<<8)|a[13])+85590281L;
  ik.b21=13836L*(S64)(S16)((a[14]<<8)|a[15])+79333336L;
  ik.bp3=2915L*(S64)(S16)((a[16]<<8)|a[17])+157155561L;}
static S16 conv(IK*ik,S32 dt){S64 wk1,wk2;wk1=((S64)ik->a1*(S64)dt);wk2=((S64)ik->a2*(S64)dt)>>14;wk2=(wk2*(S64)dt)>>10;wk2=((wk1+wk2)/32767)>>19;return (S16)((ik->a0+wk2)>>4);}
static S32 pres(IK*ik,S32 dp,S16 tx){S64 wk1,wk2,wk3;wk1=((S64)ik->bt1*(S64)tx);wk2=((S64)ik->bp1*(S64)dp)>>5;wk1+=wk2;wk2=((S64)ik->bt2*(S64)tx)>>1;wk2=(wk2*(S64)tx)>>8;wk3=wk2;wk2=((S64)ik->b11*(S64)tx)>>4;wk2=(wk2*(S64)dp)>>1;wk3+=wk2;wk2=((S64)ik->bp2*(S64)dp)>>13;wk2=(wk2*(S64)dp)>>1;wk3+=wk2;wk1+=wk3>>14;wk2=((S64)ik->b12*(S64)tx);wk2=(wk2*(S64)tx)>>22;wk2=(wk2*(S64)dp)>>1;wk3=wk2;wk2=((S64)ik->b21*(S64)tx)>>6;wk2=(wk2*(S64)dp)>>23;wk2=(wk2*(S64)dp)>>1;wk3+=wk2;wk2=((S64)ik->bp3*(S64)dp)>>12;wk2=(wk2*(S64)dp)>>23;wk2=(wk2*(S64)dp);wk3+=wk2;wk1+=wk3>>15;wk1/=32767L;wk1>>=11;wk1+=ik->b00;return (S32)wk1;}
}

void test_qmp6988_matches_m5_reference() {
  uint32_t seed = 12345;
  auto rnd = [&]() { return seed = seed * 1103515245u + 12345u, (seed >> 8); };
  int bad = 0;
  for (int i = 0; i < 5000; ++i) {
    uint8_t raw[25];
    for (auto& b : raw) b = rnd() & 0xFF;
    uint32_t rp = rnd() & 0xFFFFFF, rt = rnd() & 0xFFFFFF;
    qref::IK ik;
    qref::refCal(raw, ik);
    int16_t tx = qref::conv(&ik, (int32_t)(rt - 8388608));
    int32_t pr = qref::pres(&ik, (int32_t)(rp - 8388608), tx);
    Qmp6988Calib c;
    qmp6988ParseCalib(raw, c);
    float t, p;
    qmp6988Compensate(c, rp, rt, t, p);
    if (t != tx / 256.0f || p != pr / 16.0f) ++bad;
  }
  TEST_ASSERT_TRUE(bad == 0);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_decode_known_packet);
  RUN_TEST(test_decode_rejects_foreign_data);
  RUN_TEST(test_states_basic);
  RUN_TEST(test_timeout);
  RUN_TEST(test_hysteresis);
  RUN_TEST(test_fast_leak);
  RUN_TEST(test_slow_drop_is_no_leak);
  RUN_TEST(test_alarm_manager);
  RUN_TEST(test_settings);
  RUN_TEST(test_decode_type_a_via_advertisement);
  RUN_TEST(test_decode_type_b);
  RUN_TEST(test_decode_type_d);
  RUN_TEST(test_decode_type_f);
  RUN_TEST(test_decode_type_g_and_h);
  RUN_TEST(test_decode_ignores_other_devices);
  RUN_TEST(test_tilt_flat_sensor);
  RUN_TEST(test_tilt_vertical_mount_and_rotation);
  RUN_TEST(test_leveling_lifts);
  RUN_TEST(test_tilt_warning_hysteresis);
  RUN_TEST(test_settings_tilt_clamp);
  RUN_TEST(test_compass_heading_flat);
  RUN_TEST(test_compass_tilt_compensation);
  RUN_TEST(test_compass_correction_and_calibrator);
  RUN_TEST(test_bmp280_datasheet_example);
  RUN_TEST(test_altitude);
  RUN_TEST(test_pressure_trend);
  RUN_TEST(test_qmp6988_matches_m5_reference);
  return UNITY_END();
}
