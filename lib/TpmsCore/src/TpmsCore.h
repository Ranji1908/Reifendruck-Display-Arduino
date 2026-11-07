// TpmsCore – hardwareunabhaengige Logik fuer das Reifendruck-Display.
// Enthaelt: BLE-Sensor-Dekoder, Warnlogik pro Rad, Alarm-Quittierung, Einstellungen.
// Keine Arduino-Abhaengigkeiten -> auf dem PC per `pio test -e native` testbar.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tpms {

// ---------------------------------------------------------------- Grundtypen
enum Wheel : uint8_t { FL = 0, FR = 1, RL = 2, RR = 3, WHEEL_COUNT = 4 };
enum Mode : uint8_t { ROAD = 0, OFFROAD = 1, MODE_COUNT = 2 };

// Kurzbezeichnung fuer die Anzeige: VL, VR, HL, HR
const char* wheelName(Wheel w);
const char* modeName(Mode m);

// ---------------------------------------------------------------- Dekoder
struct SensorReading {
  uint64_t id = 0;          // 6-Byte Sensor-Adresse (aus den Nutzdaten)
  float pressureBar = 0;    // Ueberdruck in bar
  float temperatureC = 0;   // Temperatur in Grad C
  uint8_t batteryPct = 0;   // Batterie in %
  bool sensorAlarm = false; // Alarm-Flag des Sensors selbst
  int8_t rssi = 0;          // Signalstaerke (wird vom BLE-Teil gesetzt)
};

// Dekodiert die "Manufacturer Data" der verbreiteten BLE-Ventilkappen-Sensoren
// (Name meist "TPMS1_xxxxxx" .. "TPMS4_xxxxxx", Adresse 80:EA:CA:xx:xx:xx).
// Layout (18 Byte, inkl. 2 Byte Company-ID):
//   [0..1]  0x00 0x01
//   [2..7]  Sensor-Adresse
//   [8..11] Druck in Pa, uint32 little endian
//   [12..15] Temperatur in 1/100 Grad C, int32 little endian
//   [16]    Batterie %
//   [17]    Alarm-Flag
// Gibt false zurueck, wenn die Daten nicht zu diesem Format passen.
bool decodeManufacturerData(const uint8_t* data, size_t len, SensorReading& out);

// Formatiert die letzten 3 Byte der ID als "AB:CD:EF" (buf >= 9 Zeichen).
void formatShortId(uint64_t id, char* buf, size_t bufLen);

// ---------------------------------------------------------------- Einstellungen
struct Settings {
  // Solldruck [Modus][Achse] in bar, Achse 0 = vorne, 1 = hinten.
  // Platzhalter! Bitte an die Reifendrucktabelle des Fahrzeugs anpassen.
  float target[MODE_COUNT][2] = {{3.5f, 4.0f}, {2.5f, 2.8f}};
  uint8_t warnPct = 10;   // Warnung, wenn Druck > warnPct % unter Soll
  uint8_t critPct = 20;   // Kritisch, wenn Druck > critPct % unter Soll
  uint8_t highPct = 20;   // Hinweis "zu hoch", wenn > highPct % ueber Soll
  uint8_t leakDeciBar = 3;   // schneller Druckverlust: >= x/10 bar ...
  uint8_t leakMinutes = 5;   // ... innerhalb von x Minuten
  uint8_t contrast = 160;    // Display-Helligkeit 0..255
  bool buzzer = true;
  bool demo = false;
  uint8_t mode = ROAD;
};

void clampSettings(Settings& s);
float targetFor(const Settings& s, Mode m, Wheel w);

// ---------------------------------------------------------------- Warnlogik
enum class State : uint8_t {
  NotPaired,  // kein Sensor angelernt
  NoData,     // Sensor angelernt, aber seit Timeout nichts empfangen
  Ok,
  High,       // deutlich ueber Soll (nur Anzeige, kein Ton)
  LowWarn,    // unter Soll – Warnung
  LowCrit,    // stark unter Soll – Alarm
  FastLeak,   // schneller Druckverlust – Alarm
};

// 0 = nichts, 1 = Hinweis, 2 = Warnung, 3/4 = Alarm
uint8_t severity(State s);
const char* stateText(State s);

struct EvalParams {
  float warnPct = 10;
  float critPct = 20;
  float highPct = 20;
  float leakBar = 0.3f;
  uint32_t leakWindowMs = 5UL * 60UL * 1000UL;
  uint32_t timeoutMs = 20UL * 60UL * 1000UL;  // Sensoren senden im Stand selten
  float hysteresisBar = 0.05f;
};

EvalParams paramsFrom(const Settings& s);

class WheelMonitor {
 public:
  void reset();
  void addReading(const SensorReading& r, uint32_t nowMs);
  State evaluate(float targetBar, const EvalParams& p, uint32_t nowMs, bool paired);

  bool hasData() const { return hasData_; }
  float pressure() const { return pressure_; }
  float temperature() const { return temperature_; }
  uint8_t battery() const { return battery_; }
  State lastState() const { return last_; }

 private:
  static const uint8_t HISTORY = 16;
  static const uint32_t SAMPLE_SPACING_MS = 20000;  // max. 1 Verlaufspunkt / 20 s
  struct Sample {
    uint32_t t;
    float p;
  };
  bool detectLeak(const EvalParams& p, uint32_t nowMs) const;

  Sample hist_[HISTORY];
  uint8_t histCount_ = 0;
  uint8_t histHead_ = 0;  // naechster Schreibindex
  bool hasData_ = false;
  float pressure_ = 0;
  float temperature_ = 0;
  uint8_t battery_ = 0;
  uint32_t lastMs_ = 0;
  State last_ = State::NotPaired;
};

// ---------------------------------------------------------------- Quittierung
// Merkt sich pro Rad, welche Stufe bereits quittiert wurde. Ein Ton ertoent nur,
// wenn sich ein Rad gegenueber dem quittierten Stand verschlechtert.
class AlarmManager {
 public:
  enum class Sound : uint8_t { None, WarnOnce, Alarm };
  // Aktualisieren und gewuenschten Ton zurueckgeben.
  Sound update(const State states[WHEEL_COUNT]);
  void acknowledgeAll();
  bool hasUnacknowledged() const;
  // Rad mit der schlechtesten unquittierten oder aktuellen Stufe (oder WHEEL_COUNT).
  Wheel worstWheel(const State states[WHEEL_COUNT]) const;

 private:
  uint8_t current_[WHEEL_COUNT] = {0, 0, 0, 0};
  uint8_t acked_[WHEEL_COUNT] = {0, 0, 0, 0};
};

}  // namespace tpms
