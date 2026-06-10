#include "apnea_features.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "metrics_processor.h"

static bool isFiniteValue(float value) {
  return !std::isnan(value) && !std::isinf(value);
}

static bool isOlderThan(unsigned long timestamp, unsigned long now, unsigned long ageMs) {
  return now >= timestamp && (now - timestamp) > ageMs;
}

static float meanValue(const float* values, int count) {
  if (count <= 0) {
    return NAN;
  }

  double sum = 0.0;
  for (int i = 0; i < count; i++) {
    sum += values[i];
  }
  return (float)(sum / count);
}

static float sampleStdDev(const float* values, int count) {
  if (count < 2) {
    return NAN;
  }

  double mean = meanValue(values, count);
  double sum = 0.0;
  for (int i = 0; i < count; i++) {
    double diff = values[i] - mean;
    sum += diff * diff;
  }
  return (float)std::sqrt(sum / (count - 1));
}

static float minValue(const float* values, int count) {
  if (count <= 0) {
    return NAN;
  }

  float result = values[0];
  for (int i = 1; i < count; i++) {
    result = std::min(result, values[i]);
  }
  return result;
}

static float maxValue(const float* values, int count) {
  if (count <= 0) {
    return NAN;
  }

  float result = values[0];
  for (int i = 1; i < count; i++) {
    result = std::max(result, values[i]);
  }
  return result;
}

static float slopeByIndex(const float* values, int count) {
  if (count < 2) {
    return NAN;
  }

  double meanX = (count - 1) / 2.0;
  double meanY = meanValue(values, count);
  double numerator = 0.0;
  double denominator = 0.0;
  for (int i = 0; i < count; i++) {
    double dx = i - meanX;
    double dy = values[i] - meanY;
    numerator += dx * dy;
    denominator += dx * dx;
  }

  if (denominator <= 0.0) {
    return NAN;
  }
  return (float)(numerator / denominator);
}

static float rmssdValue(const float* values, int count) {
  if (count < 2) {
    return NAN;
  }

  double sum = 0.0;
  for (int i = 1; i < count; i++) {
    double diff = values[i] - values[i - 1];
    sum += diff * diff;
  }
  return (float)std::sqrt(sum / (count - 1));
}

static float pnn50Value(const float* rrValues, int count) {
  if (count < 2) {
    return NAN;
  }

  int hits = 0;
  for (int i = 1; i < count; i++) {
    if (std::fabs(rrValues[i] - rrValues[i - 1]) > 50.0f) {
      hits++;
    }
  }
  return (float)((100.0 * hits) / (count - 1));
}

static int countDesaturations(const float* spo2Values, int count) {
  if (count < 3) {
    return 0;
  }

  float recentMax = spo2Values[0];
  bool inEvent = false;
  int events = 0;

  for (int i = 1; i < count; i++) {
    float value = spo2Values[i];
    if (value > recentMax) {
      recentMax = value;
      inEvent = false;
    }

    if ((recentMax - value) >= 3.0f && !inEvent) {
      events++;
      inEvent = true;
    }

    if (inEvent && value >= (recentMax - 1.0f)) {
      inEvent = false;
      recentMax = value;
    }
  }

  return events;
}

static float percentageBelow(const float* values, int count, float threshold) {
  if (count <= 0) {
    return NAN;
  }

  int below = 0;
  for (int i = 0; i < count; i++) {
    if (values[i] < threshold) {
      below++;
    }
  }
  return (float)((100.0 * below) / count);
}

ApneaFeatureAccumulator::ApneaFeatureAccumulator() {
  reset();
}

void ApneaFeatureAccumulator::reset() {
  std::memset(rrIntervals_, 0, sizeof(rrIntervals_));
  std::memset(spo2Values_, 0, sizeof(spo2Values_));
  std::memset(windowMarks_, 0, sizeof(windowMarks_));
  rrCount_ = 0;
  spo2Count_ = 0;
  windowMarkCount_ = 0;
}

void ApneaFeatureAccumulator::addRRInterval(unsigned long timestamp, float value) {
  if (!isFiniteValue(value) || value < RR_MIN_MS || value > RR_MAX_MS) {
    return;
  }

  if (rrCount_ >= APNEA_MAX_RR_SAMPLES) {
    for (int i = 1; i < APNEA_MAX_RR_SAMPLES; i++) {
      rrIntervals_[i - 1] = rrIntervals_[i];
    }
    rrCount_ = APNEA_MAX_RR_SAMPLES - 1;
  }

  rrIntervals_[rrCount_++] = TimedValue{timestamp, value};
}

void ApneaFeatureAccumulator::addSpO2(unsigned long timestamp, float value) {
  if (!isFiniteValue(value) || value < 0.0f || value > 100.0f) {
    return;
  }

  if (spo2Count_ >= APNEA_MAX_SPO2_SAMPLES) {
    for (int i = 1; i < APNEA_MAX_SPO2_SAMPLES; i++) {
      spo2Values_[i - 1] = spo2Values_[i];
    }
    spo2Count_ = APNEA_MAX_SPO2_SAMPLES - 1;
  }

  spo2Values_[spo2Count_++] = TimedValue{timestamp, value};
}

void ApneaFeatureAccumulator::addWindowMark(unsigned long timestamp) {
  if (windowMarkCount_ >= APNEA_MAX_WINDOW_MARKS) {
    for (int i = 1; i < APNEA_MAX_WINDOW_MARKS; i++) {
      windowMarks_[i - 1] = windowMarks_[i];
    }
    windowMarkCount_ = APNEA_MAX_WINDOW_MARKS - 1;
  }

  windowMarks_[windowMarkCount_++] = timestamp;
}

void ApneaFeatureAccumulator::pruneOld(unsigned long now) {
  int writeIndex = 0;
  for (int i = 0; i < rrCount_; i++) {
    if (!isOlderThan(rrIntervals_[i].timestamp, now, APNEA_WINDOW_MS)) {
      rrIntervals_[writeIndex++] = rrIntervals_[i];
    }
  }
  rrCount_ = writeIndex;

  writeIndex = 0;
  for (int i = 0; i < spo2Count_; i++) {
    if (!isOlderThan(spo2Values_[i].timestamp, now, APNEA_WINDOW_MS)) {
      spo2Values_[writeIndex++] = spo2Values_[i];
    }
  }
  spo2Count_ = writeIndex;

  writeIndex = 0;
  for (int i = 0; i < windowMarkCount_; i++) {
    if (!isOlderThan(windowMarks_[i], now, APNEA_WINDOW_MS)) {
      windowMarks_[writeIndex++] = windowMarks_[i];
    }
  }
  windowMarkCount_ = writeIndex;
}

void ApneaFeatureAccumulator::addMetrics(const SensorMetrics& metrics,
                                         const float* recentRRIntervals,
                                         int recentRRIntervalCount) {
  pruneOld(metrics.timestamp);

  if (!metrics.fingerDetected || metrics.confidence < 2) {
    return;
  }

  addWindowMark(metrics.timestamp);
  for (int i = 0; i < recentRRIntervalCount; i++) {
    addRRInterval(metrics.timestamp, recentRRIntervals[i]);
  }
  addSpO2(metrics.timestamp, metrics.spo2);

  pruneOld(metrics.timestamp);
}

ApneaFeatures ApneaFeatureAccumulator::buildFeatures(unsigned long windowEndMs) const {
  ApneaFeatures features;
  for (int i = 0; i < APNEA_FEATURE_COUNT; i++) {
    features.values[i] = NAN;
  }
  features.rrCount = rrCount_;
  features.spo2Count = spo2Count_;
  features.windowEndMs = windowEndMs;
  features.windowStartMs = windowEndMs > APNEA_WINDOW_MS ? windowEndMs - APNEA_WINDOW_MS : 0;
  features.valid = false;
  features.status = "warming_up";

  if (windowMarkCount_ < 2 || windowMarks_[windowMarkCount_ - 1] - windowMarks_[0] < APNEA_MIN_WINDOW_SPAN_MS) {
    return features;
  }

  if (rrCount_ < 3 || spo2Count_ < 3) {
    features.status = "insufficient_signal";
    return features;
  }

  float rrValues[APNEA_MAX_RR_SAMPLES];
  float heartRateValues[APNEA_MAX_RR_SAMPLES];
  for (int i = 0; i < rrCount_; i++) {
    rrValues[i] = rrIntervals_[i].value;
    heartRateValues[i] = 60000.0f / rrValues[i];
  }

  float spo2Values[APNEA_MAX_SPO2_SAMPLES];
  for (int i = 0; i < spo2Count_; i++) {
    spo2Values[i] = spo2Values_[i].value;
  }

  float rrMean = meanValue(rrValues, rrCount_);
  float heartRateMean = meanValue(heartRateValues, rrCount_);
  float spo2Mean = meanValue(spo2Values, spo2Count_);

  features.values[0] = rrMean;
  features.values[1] = sampleStdDev(rrValues, rrCount_);
  features.values[2] = minValue(rrValues, rrCount_);
  features.values[3] = maxValue(rrValues, rrCount_);
  features.values[4] = heartRateMean;
  features.values[5] = sampleStdDev(heartRateValues, rrCount_);
  features.values[6] = minValue(heartRateValues, rrCount_);
  features.values[7] = maxValue(heartRateValues, rrCount_);
  features.values[8] = slopeByIndex(heartRateValues, rrCount_);
  features.values[9] = features.values[1];
  features.values[10] = rmssdValue(rrValues, rrCount_);
  features.values[11] = pnn50Value(rrValues, rrCount_);
  features.values[12] = spo2Mean;
  features.values[13] = sampleStdDev(spo2Values, spo2Count_);
  features.values[14] = minValue(spo2Values, spo2Count_);
  features.values[15] = maxValue(spo2Values, spo2Count_);
  features.values[16] = slopeByIndex(spo2Values, spo2Count_);
  features.values[17] = features.values[15] - features.values[14];
  features.values[18] = (float)countDesaturations(spo2Values, spo2Count_);
  features.values[19] = percentageBelow(spo2Values, spo2Count_, 90.0f);
  features.values[20] = percentageBelow(spo2Values, spo2Count_, 92.0f);
  features.values[21] = isFiniteValue(spo2Mean) && spo2Mean > 0.0f ? heartRateMean / spo2Mean : NAN;
  features.values[22] = heartRateMean * spo2Mean;
  features.valid = true;
  features.status = "ok";
  return features;
}

ApneaInference ApneaFeatureAccumulator::predict(unsigned long windowEndMs, int confidence) const {
  ApneaInference inference;
  inference.valid = false;
  inference.status = "warming_up";
  inference.prediction = -1;
  inference.label = nullptr;
  inference.apneaProbability = NAN;
  inference.confidence = confidence;
  inference.timestamp = windowEndMs;
  inference.windowSec = APNEA_WINDOW_SEC;

  ApneaFeatures features = buildFeatures(windowEndMs);
  if (!features.valid) {
    inference.status = features.status;
    return inference;
  }

  ApneaPrediction prediction = predictApnea(features.values);
  inference.valid = true;
  inference.status = "ok";
  inference.prediction = prediction.prediction;
  inference.label = prediction.prediction == 1 ? "apnea" : "non-apnea";
  inference.apneaProbability = prediction.apneaProbability;
  return inference;
}
