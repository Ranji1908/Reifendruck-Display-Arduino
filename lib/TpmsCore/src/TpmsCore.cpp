#include "TpmsCore.h"

#include <stdio.h>

namespace tpms {

const char* wheelName(Wheel w) {
  switch (w) {
    case FL: return "VL";
    case FR: return "VR";
    case RL: return "HL";
    case RR: return "HR";
    default: return "??";
  }
}

const char* modeName(Mode m) { return m == OFFROAD ? "OFFROAD" : "STRASSE"; }

// ---------------------------------------------------------------- Dekoder
static uint32_t readU32LE(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool decodeManufacturerData(const uint8_t* d, size_t len, SensorReading& out) {
  if (d == nullptr || len != 18) return false;
  if (d[0] != 0x00 || d[1] != 0x01) return false;

  uint64_t id = 0;
  for (int i = 2; i < 8; ++i) id = (id << 8) | d[i];

  uint32_t pa = readU32LE(d + 8);
  int32_t centiC = (int32_t)readU32LE(d + 12);

  float bar = pa / 100000.0f;
  float tempC = centiC / 100.0f;
  // Plausibilitaet: kaputte/fremde Pakete verwerfen
  if (bar < 0.0f || bar > 12.0f) return false;
  if (tempC < -50.0f || tempC > 150.0f) return false;

  out.id = id;
  out.pressureBar = bar;
  out.temperatureC = tempC;
  out.batteryPct = d[16] > 100 ? 100 : d[16];
  out.sensorAlarm = d[17] != 0;
  return true;
}

void formatShortId(uint64_t id, char* buf, size_t bufLen) {
  snprintf(buf, bufLen, "%02X:%02X:%02X", (unsigned)((id >> 16) & 0xFF), (unsigned)((id >> 8) & 0xFF),
           (unsigned)(id & 0xFF));
}

// ---------------------------------------------------------------- Einstellungen
template <typename T>
static T clampT(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

void clampSettings(Settings& s) {
  for (int m = 0; m < MODE_COUNT; ++m)
    for (int a = 0; a < 2; ++a) {
      float v = s.target[m][a];
      if (!(v == v)) v = 3.0f;  // NaN (z.B. kaputter Speicher)
      s.target[m][a] = clampT(v, 0.5f, 8.0f);
    }
  s.warnPct = clampT<uint8_t>(s.warnPct, 3, 40);
  s.critPct = clampT<uint8_t>(s.critPct, (uint8_t)(s.warnPct + 2), 60);
  s.highPct = clampT<uint8_t>(s.highPct, 5, 60);
  s.leakDeciBar = clampT<uint8_t>(s.leakDeciBar, 1, 20);
  s.leakMinutes = clampT<uint8_t>(s.leakMinutes, 1, 15);
  if (s.mode >= MODE_COUNT) s.mode = ROAD;
}

float targetFor(const Settings& s, Mode m, Wheel w) { return s.target[m][w < RL ? 0 : 1]; }

EvalParams paramsFrom(const Settings& s) {
  EvalParams p;
  p.warnPct = s.warnPct;
  p.critPct = s.critPct;
  p.highPct = s.highPct;
  p.leakBar = s.leakDeciBar / 10.0f;
  p.leakWindowMs = (uint32_t)s.leakMinutes * 60UL * 1000UL;
  return p;
}

// ---------------------------------------------------------------- Warnlogik
uint8_t severity(State s) {
  switch (s) {
    case State::High: return 1;
    case State::LowWarn: return 2;
    case State::LowCrit: return 3;
    case State::FastLeak: return 4;
    default: return 0;
  }
}

const char* stateText(State s) {
  switch (s) {
    case State::NotPaired: return "anlernen";
    case State::NoData: return "k.Signal";
    case State::Ok: return "OK";
    case State::High: return "HOCH";
    case State::LowWarn: return "NIEDRIG";
    case State::LowCrit: return "KRITISCH";
    case State::FastLeak: return "LECK!";
  }
  return "";
}

void WheelMonitor::reset() { *this = WheelMonitor(); }

void WheelMonitor::addReading(const SensorReading& r, uint32_t nowMs) {
  hasData_ = true;
  pressure_ = r.pressureBar;
  temperature_ = r.temperatureC;
  battery_ = r.batteryPct;
  lastMs_ = nowMs;

  // Verlauf fuer die Leck-Erkennung, ausgeduennt
  if (histCount_ > 0) {
    const Sample& newest = hist_[(histHead_ + HISTORY - 1) % HISTORY];
    if (nowMs - newest.t < SAMPLE_SPACING_MS) return;
  }
  hist_[histHead_] = {nowMs, r.pressureBar};
  histHead_ = (histHead_ + 1) % HISTORY;
  if (histCount_ < HISTORY) ++histCount_;
}

bool WheelMonitor::detectLeak(const EvalParams& p, uint32_t nowMs) const {
  // Hoechster Druck im Zeitfenster vs. aktueller Druck
  float maxP = -1;
  for (uint8_t i = 0; i < histCount_; ++i) {
    const Sample& s = hist_[i];
    if (nowMs - s.t <= p.leakWindowMs && s.p > maxP) maxP = s.p;
  }
  return maxP >= 0 && (maxP - pressure_) >= p.leakBar;
}

State WheelMonitor::evaluate(float target, const EvalParams& p, uint32_t nowMs, bool paired) {
  State s;
  if (!paired) {
    s = State::NotPaired;
  } else if (!hasData_ || nowMs - lastMs_ > p.timeoutMs) {
    s = State::NoData;
  } else {
    const float h = p.hysteresisBar;
    const float tWarn = target * (1.0f - p.warnPct / 100.0f);
    const float tCrit = target * (1.0f - p.critPct / 100.0f);
    const float tHigh = target * (1.0f + p.highPct / 100.0f);
    const float pr = pressure_;
    const bool wasLow = last_ == State::LowWarn || last_ == State::LowCrit || last_ == State::FastLeak;

    if (detectLeak(p, nowMs) || (last_ == State::FastLeak && pr < tWarn + h)) {
      s = State::FastLeak;  // bleibt, bis der Druck wieder im Sollbereich ist
    } else if (pr < tCrit || ((last_ == State::LowCrit) && pr < tCrit + h)) {
      s = State::LowCrit;
    } else if (pr < tWarn || (wasLow && pr < tWarn + h)) {
      s = State::LowWarn;
    } else if (pr > tHigh || (last_ == State::High && pr > tHigh - h)) {
      s = State::High;
    } else {
      s = State::Ok;
    }
  }
  last_ = s;
  return s;
}

// ---------------------------------------------------------------- Quittierung
AlarmManager::Sound AlarmManager::update(const State states[WHEEL_COUNT]) {
  uint8_t worstNew = 0;
  for (int i = 0; i < WHEEL_COUNT; ++i) {
    current_[i] = severity(states[i]);
    if (current_[i] < acked_[i]) acked_[i] = current_[i];  // Besserung -> erneut scharf
    if (current_[i] > acked_[i] && current_[i] > worstNew) worstNew = current_[i];
  }
  if (worstNew >= 3) return Sound::Alarm;  // bis zur Quittierung
  // Hinweise und Warnungen gelten nach einmaligem Signal als quittiert
  for (int i = 0; i < WHEEL_COUNT; ++i)
    if (current_[i] > acked_[i]) acked_[i] = current_[i];
  return worstNew == 2 ? Sound::WarnOnce : Sound::None;
}

void AlarmManager::acknowledgeAll() {
  for (int i = 0; i < WHEEL_COUNT; ++i) acked_[i] = current_[i];
}

bool AlarmManager::hasUnacknowledged() const {
  for (int i = 0; i < WHEEL_COUNT; ++i)
    if (current_[i] >= 3 && current_[i] > acked_[i]) return true;
  return false;
}

Wheel AlarmManager::worstWheel(const State states[WHEEL_COUNT]) const {
  Wheel best = WHEEL_COUNT;
  uint8_t bestSev = 0;
  for (int i = 0; i < WHEEL_COUNT; ++i) {
    uint8_t s = severity(states[i]);
    if (s > bestSev) {
      bestSev = s;
      best = (Wheel)i;
    }
  }
  return best;
}

}  // namespace tpms
