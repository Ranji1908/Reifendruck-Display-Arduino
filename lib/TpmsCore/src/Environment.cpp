// Kompass, Luftdruck/Hoehe und Wettertrend – hardwareunabhaengig.
#include <math.h>
#include <string.h>

#include "TpmsCore.h"

namespace tpms {

static const float RAD2DEG = 57.2957795f;

static float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static Vec3 scale(Vec3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
static Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static bool normalize(Vec3& a) {
  float n = sqrtf(dot(a, a));
  if (n < 1e-6f) return false;
  a = scale(a, 1.0f / n);
  return true;
}
static float wrap360(float d) {
  d = fmodf(d, 360.0f);
  return d < 0 ? d + 360.0f : d;
}

// ---------------------------------------------------------------- Kompass
float TiltCalc::heading(Vec3 mag, Vec3 accel) const {
  Vec3 u = accel;
  if (!normalize(u)) u = up_;
  Vec3 f = sub(fwd_, scale(u, dot(fwd_, u)));  // Fahrzeug-Vorwaerts in der Horizontalen
  if (!normalize(f)) return 0;
  Vec3 l = cross(u, f);
  Vec3 mh = sub(mag, scale(u, dot(mag, u)));  // horizontaler Anteil zeigt nach Norden
  // Kurs = Winkel von Nord zur Fahrzeugfront im Uhrzeigersinn
  return wrap360(atan2f(dot(mh, l), dot(mh, f)) * RAD2DEG);
}

void MagCalibrator::reset() { *this = MagCalibrator(); }

void MagCalibrator::add(Vec3 m) {
  if (m.x < min_.x) min_.x = m.x;
  if (m.y < min_.y) min_.y = m.y;
  if (m.z < min_.z) min_.z = m.z;
  if (m.x > max_.x) max_.x = m.x;
  if (m.y > max_.y) max_.y = m.y;
  if (m.z > max_.z) max_.z = m.z;
}

bool MagCalibrator::usable() const {
  int spread = 0;
  if (max_.x - min_.x > 20.0f) ++spread;  // uT; Erdfeld ca. 50 uT
  if (max_.y - min_.y > 20.0f) ++spread;
  if (max_.z - min_.z > 20.0f) ++spread;
  return spread >= 2;
}

Vec3 MagCalibrator::offset() const {
  return {(min_.x + max_.x) / 2, (min_.y + max_.y) / 2, (min_.z + max_.z) / 2};
}

void MagCalibrator::addHeadingSample(float h) { sectors_ |= (uint16_t)(1u << ((int)(wrap360(h) / 22.5f) & 15)); }

float MagCalibrator::coverage() const {
  int n = 0;
  for (int i = 0; i < 16; ++i) n += (sectors_ >> i) & 1;
  return n / 16.0f;
}

float applyCompassCorrection(float h, int16_t offsetDeg, bool mirror) {
  if (mirror) h = 360.0f - h;
  return wrap360(h + offsetDeg);
}

const char* compassPoint(float h) {
  static const char* const P[8] = {"N", "NO", "O", "SO", "S", "SW", "W", "NW"};
  return P[(int)((wrap360(h) + 22.5f) / 45.0f) & 7];
}

// ---------------------------------------------------------------- BMP280
static uint16_t u16le(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

void bmp280ParseCalib(const uint8_t r[24], Bmp280Calib& c) {
  c.T1 = u16le(r + 0);
  c.T2 = (int16_t)u16le(r + 2);
  c.T3 = (int16_t)u16le(r + 4);
  c.P1 = u16le(r + 6);
  c.P2 = (int16_t)u16le(r + 8);
  c.P3 = (int16_t)u16le(r + 10);
  c.P4 = (int16_t)u16le(r + 12);
  c.P5 = (int16_t)u16le(r + 14);
  c.P6 = (int16_t)u16le(r + 16);
  c.P7 = (int16_t)u16le(r + 18);
  c.P8 = (int16_t)u16le(r + 20);
  c.P9 = (int16_t)u16le(r + 22);
}

bool bmp280Compensate(const Bmp280Calib& c, int32_t adcT, int32_t adcP, float& tempC, float& pressPa) {
  // Formeln aus dem Bosch-Datenblatt (Gleitkomma-Variante)
  double v1 = (adcT / 16384.0 - c.T1 / 1024.0) * c.T2;
  double v2 = (adcT / 131072.0 - c.T1 / 8192.0);
  v2 = v2 * v2 * c.T3;
  double tFine = v1 + v2;
  tempC = (float)(tFine / 5120.0);

  v1 = tFine / 2.0 - 64000.0;
  v2 = v1 * v1 * c.P6 / 32768.0;
  v2 = v2 + v1 * c.P5 * 2.0;
  v2 = v2 / 4.0 + c.P4 * 65536.0;
  v1 = (c.P3 * v1 * v1 / 524288.0 + c.P2 * v1) / 524288.0;
  v1 = (1.0 + v1 / 32768.0) * c.P1;
  if (v1 == 0.0) return false;
  double p = 1048576.0 - adcP;
  p = (p - v2 / 4096.0) * 6250.0 / v1;
  v1 = c.P9 * p * p / 2147483648.0;
  v2 = p * c.P8 / 32768.0;
  p = p + (v1 + v2 + c.P7) / 16.0;
  pressPa = (float)p;
  return pressPa > 30000.0f && pressPa < 120000.0f;
}

float altitudeFromPressure(float pa, float qnh) { return 44330.0f * (1.0f - powf(pa / qnh, 0.1902949f)); }

float qnhFromAltitude(float pa, float alt) { return pa / powf(1.0f - alt / 44330.0f, 5.255f); }

// ---------------------------------------------------------------- Trend
void PressureTrend::add(float pa, uint32_t now) {
  if (count_ > 0) {
    uint32_t newest = t_[(head_ + N - 1) % N];
    if (now - newest < 600000UL) return;  // alle 10 min
  }
  p_[head_] = pa;
  t_[head_] = now;
  head_ = (head_ + 1) % N;
  if (count_ < N) ++count_;
}

bool PressureTrend::valid() const {
  if (count_ < 2) return false;
  uint32_t oldest = t_[(head_ + N - count_) % N], newest = t_[(head_ + N - 1) % N];
  return newest - oldest >= 3600000UL;
}

float PressureTrend::changeHpa3h() const {
  if (!valid()) return 0;
  uint8_t io = (head_ + N - count_) % N, in = (head_ + N - 1) % N;
  float hours = (t_[in] - t_[io]) / 3600000.0f;
  return (p_[in] - p_[io]) / 100.0f * (3.0f / hours);
}

const char* PressureTrend::text() const {
  if (!valid()) return "--";
  float c = changeHpa3h();
  if (c <= -3.0f) return "STURM?";
  if (c <= -1.0f) return "FAELLT";
  if (c >= 1.0f) return "STEIGT";
  return "STABIL";
}

}  // namespace tpms
