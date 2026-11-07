// Dekoder fuer verbreitete BLE-Ventilkappen-Reifensensoren.
// Die Formate sind dem Home-Assistant-Projekt github.com/bkbilly/tpms_ble entnommen
// (dort mit Mitschnitten echter Sensoren belegt) und hier fuer Mikrocontroller neu umgesetzt.
#include <string.h>

#include "TpmsCore.h"

namespace tpms {

static const float PSI_TO_BAR = 0.0689476f;

const char* sensorTypeName(SensorType t) {
  switch (t) {
    case SensorType::TypeA: return "TPMSII";
    case SensorType::TypeB: return "SYTPMS";
    case SensorType::TypeD: return "ITPMS";
    case SensorType::TypeF: return "HRTPMS";
    case SensorType::TypeG: return "AiTPMS";
    case SensorType::TypeH: return "TC.TPMS";
    default: return "?";
  }
}

bool Advertisement::hasService(uint16_t uuid) const {
  for (uint8_t i = 0; i < serviceCount && i < 4; ++i)
    if (services[i] == uuid) return true;
  return false;
}

uint8_t batteryPercentFromVoltage(float v) {
  static const float curve[][2] = {{3.3f, 100}, {3.05f, 97}, {2.94f, 91}, {2.9f, 75},
                                   {2.85f, 25}, {2.8f, 17},  {2.6f, 0}};
  const int n = sizeof(curve) / sizeof(curve[0]);
  if (v >= curve[0][0]) return 100;
  if (v < curve[n - 1][0]) return 0;
  for (int i = 0; i < n - 1; ++i) {
    float v2 = curve[i][0], p2 = curve[i][1], v1 = curve[i + 1][0], p1 = curve[i + 1][1];
    if (v > v1 && v <= v2) return (uint8_t)(p1 + (v - v1) / (v2 - v1) * (p2 - p1) + 0.5f);
  }
  return 0;
}

static uint32_t u32le(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t u16be(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

static bool plausible(float bar, float tempC) {
  return bar >= 0.0f && bar <= 12.0f && tempC >= -50.0f && tempC <= 150.0f;
}

static bool finish(SensorReading& out, uint64_t id, float bar, float tempC, uint8_t batt, bool alarm,
                   SensorType t) {
  if (bar < 0.0f) bar = 0.0f;  // Unterdruck-Rauschen bei abgeschraubtem Sensor
  if (!plausible(bar, tempC)) return false;
  out.id = id;
  out.pressureBar = bar;
  out.temperatureC = tempC;
  out.batteryPct = batt > 100 ? 100 : batt;
  out.sensorAlarm = alarm;
  out.type = t;
  return true;
}

// ---- Type A: TPMSII (Company 0x0100, 16 Byte Nutzdaten)
bool decodeManufacturerData(const uint8_t* d, size_t len, SensorReading& out) {
  if (d == nullptr || len != 18) return false;
  if (d[0] != 0x00 || d[1] != 0x01) return false;
  uint64_t id = 0;
  for (int i = 2; i < 8; ++i) id = (id << 8) | d[i];
  float bar = u32le(d + 8) / 100000.0f;
  float tempC = (int32_t)u32le(d + 12) / 100.0f;
  if (!plausible(bar, tempC)) return false;
  return finish(out, id, bar, tempC, d[16], d[17] != 0, SensorType::TypeA);
}

// ---- Type B: SYTPMS (Service 0x27A5). Company-ID-High-Byte = Spannung*10, 5 Byte Nutzdaten
static bool decodeB(const Advertisement& a, SensorReading& out) {
  if (a.mfrLen != 7) return false;
  const uint8_t* d = a.mfr + 2;
  float volts = a.mfr[1] / 10.0f;
  float tempC = (int8_t)d[0];
  float psi = ((int)u16be(d + 1) - 145) / 10.0f;
  return finish(out, a.address, psi * PSI_TO_BAR, tempC, batteryPercentFromVoltage(volts), false,
                SensorType::TypeB);
}

// ---- Type D: ITPMS (K) / WODHMIEY (Company 172)
static bool decodeD(const Advertisement& a, SensorReading& out) {
  if (a.mfrLen < 2 + 7) return false;
  const uint8_t* d = a.mfr + 2;
  size_t n = a.mfrLen - 2;
  float volts = d[0] * 0.01f + 1.22f;
  int pct = (int)((volts - 1.8f) / 1.2f * 100.0f);
  pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
  float tempC = (float)d[2] - 55.0f;
  float bar;
  if (d[6] == 0 || d[6] == 1)
    bar = (d[1] + (d[6] == 1 ? 256 : 0)) * 0.03144f;
  else if (n >= 8)
    bar = ((float)d[7] - 2.0f) / 103.0f;
  else
    return false;
  return finish(out, a.address, bar, tempC, (uint8_t)pct, d[3] != 0, SensorType::TypeD);
}

// ---- Type F: HRTPMS (Service 0xA827), 9 Byte Nutzdaten
static bool decodeF(const Advertisement& a, SensorReading& out) {
  if (a.mfrLen != 2 + 9) return false;
  const uint8_t* d = a.mfr + 2;
  float volts = d[0] * 0.01f + 1.22f;
  float tempC = (float)d[1] - 50.0f;
  float bar = ((int)u16be(d + 2) - 100) * 0.01f;
  return finish(out, a.address, bar, tempC, batteryPercentFromVoltage(volts), false, SensorType::TypeF);
}

// ---- Type G (Ai TPMS) und H (TC.TPMS): Messwerte stecken teils in der Company-ID.
// Frame = 2 Byte Company-ID + Nutzdaten: [1] Spannung*10, [2] Temp, [3..4] 0,1 psi ABSOLUT
static bool decodeGH(const Advertisement& a, SensorReading& out, SensorType t) {
  const size_t want = (t == SensorType::TypeG) ? 5 : 11;
  if (a.mfrLen != want) return false;
  const uint8_t* f = a.mfr;
  float volts = f[1] / 10.0f;
  float tempC = f[2];
  float psiAbs = u16be(f + 3) / 10.0f;
  float ambient = (t == SensorType::TypeG) ? 14.696f : 14.6f;
  float psi = psiAbs - ambient;
  return finish(out, a.address, (psi > 0 ? psi : 0) * PSI_TO_BAR, tempC, batteryPercentFromVoltage(volts),
                false, t);
}

bool decodeAdvertisement(const Advertisement& a, SensorReading& out) {
  if (a.mfr == nullptr || a.mfrLen < 2) return false;
  const char* name = a.name ? a.name : "";

  if (strncmp(name, "AI-", 3) == 0 || (a.address >> 24) == 0x1230AFULL) return decodeGH(a, out, SensorType::TypeG);
  if (a.hasService(0xA828)) return decodeGH(a, out, SensorType::TypeH);
  if (a.hasService(0x27A5)) return decodeB(a, out);
  if (a.hasService(0xA827)) return decodeF(a, out);

  uint16_t company = (uint16_t)(a.mfr[0] | (a.mfr[1] << 8));
  if (company == 0x0100) return decodeManufacturerData(a.mfr, a.mfrLen, out);
  if (company == 172) return decodeD(a, out);
  return false;
}

}  // namespace tpms
