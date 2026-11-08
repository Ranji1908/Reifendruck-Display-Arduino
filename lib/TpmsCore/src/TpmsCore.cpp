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

// Dekoder: siehe TpmsDecoders.cpp

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
  s.mountQuarter &= 3;
  s.tiltWarnRoll = clampT<uint8_t>(s.tiltWarnRoll, 5, 45);
  s.tiltWarnPitch = clampT<uint8_t>(s.tiltWarnPitch, 5, 45);
  s.wheelbaseCm = clampT<uint16_t>(s.wheelbaseCm, 150, 600);
  s.trackCm = clampT<uint16_t>(s.trackCm, 100, 250);
  if (s.theme > 1) s.theme = 0;
  s.compassOffsetDeg = clampT<int16_t>(s.compassOffsetDeg, -180, 180);
  if (!(s.qnhPa > 85000.0f && s.qnhPa < 110000.0f)) s.qnhPa = 101325.0f;
  for (int i = 0; i < 3; ++i)
    if (!(s.magOffset[i] > -2000.0f && s.magOffset[i] < 2000.0f)) {
      s.magOffset[0] = s.magOffset[1] = s.magOffset[2] = 0;
      s.magCalibrated = false;
    }
  float n2 = s.level[0] * s.level[0] + s.level[1] * s.level[1] + s.level[2] * s.level[2];
  if (!(n2 > 0.25f && n2 < 4.0f)) {  // kein sinnvoller Schwerkraftvektor -> zuruecksetzen
    s.level[0] = 0;
    s.level[1] = 0;
    s.level[2] = 1;
  }
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
