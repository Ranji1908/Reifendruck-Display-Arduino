// Unit-Tests fuer die Kernlogik. Ausfuehren mit:  pio test -e native
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
  return UNITY_END();
}
