#include <math.h>

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

bool TiltCalc::setup(Vec3 level, uint8_t mountQuarter) {
  Vec3 up = level;
  if (!normalize(up)) return false;
  // Vorwaerts = Sensor-X-Achse, in die Horizontale projiziert (falls X senkrecht steht: Y)
  Vec3 ref = fabsf(up.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
  Vec3 fwd = sub(ref, scale(up, dot(ref, up)));
  if (!normalize(fwd)) return false;
  for (uint8_t q = 0; q < (mountQuarter & 3); ++q) fwd = cross(up, fwd);  // +90 Grad um Hochachse
  up_ = up;
  fwd_ = fwd;
  left_ = cross(up, fwd);
  return true;
}

void TiltCalc::compute(Vec3 a, float& rollDeg, float& pitchDeg) const {
  // Der Beschleunigungssensor misst im Stand die Gegenkraft zur Schwerkraft, also "oben".
  float u = dot(a, up_), f = dot(a, fwd_), l = dot(a, left_);
  pitchDeg = atan2f(f, sqrtf(u * u + l * l)) * RAD2DEG;
  rollDeg = atan2f(l, u) * RAD2DEG;
}

void levelingLifts(float rollDeg, float pitchDeg, float wheelbaseCm, float trackCm, float lift[WHEEL_COUNT]) {
  const float tp = tanf(pitchDeg / RAD2DEG), tr = tanf(rollDeg / RAD2DEG);
  const float x[WHEEL_COUNT] = {wheelbaseCm / 2, wheelbaseCm / 2, -wheelbaseCm / 2, -wheelbaseCm / 2};
  const float y[WHEEL_COUNT] = {trackCm / 2, -trackCm / 2, trackCm / 2, -trackCm / 2};  // links = +
  float h[WHEEL_COUNT], hmax = -1e9f;
  for (int i = 0; i < WHEEL_COUNT; ++i) {
    h[i] = x[i] * tp + y[i] * tr;
    if (h[i] > hmax) hmax = h[i];
  }
  for (int i = 0; i < WHEEL_COUNT; ++i) lift[i] = hmax - h[i];
}

bool tiltWarning(bool was, float roll, float pitch, float warnRoll, float warnPitch) {
  const float hyst = was ? 2.0f : 0.0f;
  return fabsf(roll) >= warnRoll - hyst || fabsf(pitch) >= warnPitch - hyst;
}

}  // namespace tpms
