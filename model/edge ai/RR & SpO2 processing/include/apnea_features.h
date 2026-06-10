#pragma once

#include "apnea_model.h"
#include "metrics_types.h"

constexpr int APNEA_WINDOW_SEC = 60;
constexpr unsigned long APNEA_WINDOW_MS = APNEA_WINDOW_SEC * 1000UL;
constexpr unsigned long APNEA_MIN_WINDOW_SPAN_MS = 55000UL;
constexpr int APNEA_MAX_RR_SAMPLES = 256;
constexpr int APNEA_MAX_SPO2_SAMPLES = 32;
constexpr int APNEA_MAX_WINDOW_MARKS = 32;

struct ApneaFeatures {
  float values[APNEA_FEATURE_COUNT];
  int rrCount;
  int spo2Count;
  unsigned long windowStartMs;
  unsigned long windowEndMs;
  bool valid;
  const char* status;
};

struct ApneaInference {
  bool valid;
  const char* status;
  int prediction;
  const char* label;
  float apneaProbability;
  int confidence;
  unsigned long timestamp;
  int windowSec;
};

class ApneaFeatureAccumulator {
 public:
  ApneaFeatureAccumulator();

  void reset();
  void addMetrics(const SensorMetrics& metrics, const float* recentRRIntervals, int recentRRIntervalCount);
  ApneaFeatures buildFeatures(unsigned long windowEndMs) const;
  ApneaInference predict(unsigned long windowEndMs, int confidence) const;

 private:
  struct TimedValue {
    unsigned long timestamp;
    float value;
  };

  void addRRInterval(unsigned long timestamp, float value);
  void addSpO2(unsigned long timestamp, float value);
  void addWindowMark(unsigned long timestamp);
  void pruneOld(unsigned long now);

  TimedValue rrIntervals_[APNEA_MAX_RR_SAMPLES];
  int rrCount_;
  TimedValue spo2Values_[APNEA_MAX_SPO2_SAMPLES];
  int spo2Count_;
  unsigned long windowMarks_[APNEA_MAX_WINDOW_MARKS];
  int windowMarkCount_;
};
