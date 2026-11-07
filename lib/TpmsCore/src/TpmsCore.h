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
// Unterstuetzte Sensor-Familien (Formate nach github.com/bkbilly/tpms_ble):
enum class SensorType : uint8_t {
  Unknown = 0,
  TypeA,  // App "TPMSII", Name "TPMS1_xxxxxx", Company-ID 0x0100
  TypeB,  // App "SYTPMS", Service-UUID 0x27A5
  TypeD,  // App "ITPMS (K)" / "LYTPMS", WODHMIEY, Company-ID 0x00AC (172)
  TypeF,  // "HRTPMS", Service-UUID 0xA827
  TypeG,  // App "Ai TPMS" (AI-8000), Name "AI-..." bzw. Adresse 12:30:AF:..
  TypeH,  // "TC.TPMS", Service-UUID 0xA828
};
const char* sensorTypeName(SensorType t);

struct SensorReading {
  uint64_t id = 0;          // Sensor-ID (BLE-Adresse bzw. Adresse aus den Nutzdaten)
  float pressureBar = 0;    // Ueberdruck in bar
  float temperatureC = 0;   // Temperatur in Grad C
  uint8_t batteryPct = 0;   // Batterie in %
  bool sensorAlarm = false; // Alarm-Flag des Sensors selbst
  int8_t rssi = 0;          // Signalstaerke (wird vom BLE-Teil gesetzt)
  SensorType type = SensorType::Unknown;
};

// Rohdaten einer BLE-Werbung, vom BLE-Stack befuellt.
struct Advertisement {
  uint64_t address = 0;          // BLE-Adresse, z.B. 0x1230AF010203 fuer 12:30:AF:01:02:03
  const uint8_t* mfr = nullptr;  // Manufacturer Data INKLUSIVE 2 Byte Company-ID (little endian)
  size_t mfrLen = 0;
  const char* name = "";         // Geraetename (darf leer sein)
  uint16_t services[4] = {0, 0, 0, 0};  // beworbene 16-Bit-Service-UUIDs
  uint8_t serviceCount = 0;
  bool hasService(uint16_t uuid) const;
};

// Erkennt das Sensorformat und dekodiert. false = kein (bekannter) Reifensensor.
bool decodeAdvertisement(const Advertisement& adv, SensorReading& out);

// Nur Type A (der verbreitetste). 18 Byte inkl. Company-ID:
//   [0..1] 0x00 0x01 | [2..7] Adresse | [8..11] Druck Pa (u32 LE)
//   [12..15] Temperatur 1/100 Grad (i32 LE) | [16] Batterie % | [17] Alarm
bool decodeManufacturerData(const uint8_t* data, size_t len, SensorReading& out);

// Batterie-% aus Zellspannung (CR2032-Entladekurve)
uint8_t batteryPercentFromVoltage(float volts);

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
  // --- Neigungsmesser
  float level[3] = {0.0f, 0.0f, 1.0f};  // Schwerkraftvektor des Sensors bei ebenem Fahrzeug
  uint8_t mountQuarter = 0;             // Einbaulage: Vorwaertsachse um 0/90/180/270 Grad gedreht
  uint8_t tiltWarnRoll = 25;            // Warnung Seitenneigung ab ... Grad
  uint8_t tiltWarnPitch = 30;           // Warnung Laengsneigung ab ... Grad
  uint16_t wheelbaseCm = 367;           // Radstand (W906: 325 / 367 / 432 cm)
  uint16_t trackCm = 171;               // Spurweite
  uint8_t volume = 160;                 // Lautstaerke 0..255
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

// ---------------------------------------------------------------- Neigung
struct Vec3 {
  float x, y, z;
};

// Rechnet Beschleunigungswerte eines beliebig eingebauten Lagesensors in
// Fahrzeug-Neigungswinkel um. Kalibriert wird im Stand auf ebener Flaeche.
class TiltCalc {
 public:
  // level: Messwert bei ebenem Fahrzeug. mountQuarter: Vorwaertsrichtung in 90-Grad-Schritten
  // gegenueber der Sensor-X-Achse drehen (falls Neigung "falsch herum" angezeigt wird).
  bool setup(Vec3 level, uint8_t mountQuarter);
  // roll  > 0: rechte Seite tiefer.   pitch > 0: Front hoeher (bergauf).
  void compute(Vec3 accel, float& rollDeg, float& pitchDeg) const;

 private:
  Vec3 up_{0, 0, 1}, fwd_{1, 0, 0}, left_{0, 1, 0};
};

// Wie viel cm muss jedes Rad angehoben werden (Auffahrkeil), damit das Fahrzeug eben steht?
// Das hoechste Rad bekommt 0. Reihenfolge wie Wheel: VL, VR, HL, HR.
void levelingLifts(float rollDeg, float pitchDeg, float wheelbaseCm, float trackCm, float liftCm[WHEEL_COUNT]);

// Warnung mit 2 Grad Hysterese
bool tiltWarning(bool wasWarning, float rollDeg, float pitchDeg, float warnRoll, float warnPitch);

}  // namespace tpms
