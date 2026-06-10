#include "epoch_accumulator.h"

#include <cmath>
#include <cstring>

#include "metrics_processor.h"

static float arrMean(const float* values, int count) {
  if (count <= 0) return NAN;
  double sum = 0.0;
  for (int i = 0; i < count; i++) sum += values[i];
  return (float)(sum / count);
}

static float arrStd(const float* values, int count) {
  if (count < 2) return NAN;
  double mean = arrMean(values, count);
  double sum = 0.0;
  for (int i = 0; i < count; i++) {
    double diff = values[i] - mean;
    sum += diff * diff;
  }
  return (float)std::sqrt(sum / (count - 1));
}

static float arrMin(const float* values, int count) {
  if (count <= 0) return NAN;
  float result = values[0];
  for (int i = 1; i < count; i++) {
    if (values[i] < result) result = values[i];
  }
  return result;
}

static float arrMax(const float* values, int count) {
  if (count <= 0) return NAN;
  float result = values[0];
  for (int i = 1; i < count; i++) {
    if (values[i] > result) result = values[i];
  }
  return result;
}

static int countDesaturations(const float* spo2, int count) {
  if (count < 3) return 0;
  float recentMax = spo2[0];
  bool inEvent = false;
  int events = 0;
  for (int i = 1; i < count; i++) {
    if (spo2[i] > recentMax) {
      recentMax = spo2[i];
      inEvent = false;
    }
    if ((recentMax - spo2[i]) >= 3.0f && !inEvent) {
      events++;
      inEvent = true;
    }
    if (inEvent && spo2[i] >= (recentMax - 1.0f)) {
      inEvent = false;
      recentMax = spo2[i];
    }
  }
  return events;
}

void EpochAccumulator::reset() {
  count_ = 0;
  totalCount_ = 0;
  epochStartMs_ = 0;
}

void EpochAccumulator::addMetrics(const SensorMetrics& metrics) {
  if (epochStartMs_ == 0) {
    epochStartMs_ = metrics.timestamp;
  }

  totalCount_++;

  if (!metrics.fingerDetected || !isValidMetric(metrics.bpm) || !isValidMetric(metrics.spo2)) {
    if (count_ < EPOCH_MAX_SAMPLES) {
      fingerValues_[count_] = false;
    }
    return;
  }

  if (count_ >= EPOCH_MAX_SAMPLES) return;

  hrValues_[count_] = metrics.bpm;
  spo2Values_[count_] = metrics.spo2;
  rrValues_[count_] = metrics.respiratoryRate;
  confidenceValues_[count_] = (float)metrics.confidence;
  fingerValues_[count_] = true;
  hrvSdnn_[count_] = metrics.sdnn;
  hrvRmssd_[count_] = metrics.rmssd;
  hrvPnn50_[count_] = metrics.pnn50;
  hrvMeanRR_[count_] = metrics.meanRR;
  count_++;
}

bool EpochAccumulator::isReady(unsigned long nowMs) const {
  if (epochStartMs_ == 0) return false;
  return (nowMs - epochStartMs_) >= (unsigned long)(EPOCH_WINDOW_SEC * 1000UL);
}

EpochStats EpochAccumulator::buildAndReset(unsigned long nowMs) {
  EpochStats stats;
  std::memset(&stats, 0, sizeof(stats));
  stats.ready = false;

  if (count_ < 3) {
    reset();
    return stats;
  }

  stats.epochStartMs = epochStartMs_;
  stats.epochEndMs = nowMs;
  stats.validSamples = count_;
  stats.totalSamples = totalCount_;

  stats.hrMean = arrMean(hrValues_, count_);
  stats.hrMin = arrMin(hrValues_, count_);
  stats.hrMax = arrMax(hrValues_, count_);
  stats.hrStd = arrStd(hrValues_, count_);

  stats.spo2Mean = arrMean(spo2Values_, count_);
  stats.spo2Min = arrMin(spo2Values_, count_);
  stats.spo2Max = arrMax(spo2Values_, count_);
  stats.spo2Std = arrStd(spo2Values_, count_);
  stats.spo2DesatCount = countDesaturations(spo2Values_, count_);

  // HRV: use the last valid HRV values
  int hrvCount = 0;
  float sdnnArr[EPOCH_MAX_SAMPLES];
  float rmssdArr[EPOCH_MAX_SAMPLES];
  float pnn50Arr[EPOCH_MAX_SAMPLES];
  float meanRRArr[EPOCH_MAX_SAMPLES];
  for (int i = 0; i < count_; i++) {
    if (isValidMetric(hrvSdnn_[i])) {
      sdnnArr[hrvCount] = hrvSdnn_[i];
      rmssdArr[hrvCount] = hrvRmssd_[i];
      pnn50Arr[hrvCount] = hrvPnn50_[i];
      meanRRArr[hrvCount] = hrvMeanRR_[i];
      hrvCount++;
    }
  }
  stats.hrvSdnn = arrMean(sdnnArr, hrvCount);
  stats.hrvRmssd = arrMean(rmssdArr, hrvCount);
  stats.hrvPnn50 = arrMean(pnn50Arr, hrvCount);
  stats.hrvMeanRR = arrMean(meanRRArr, hrvCount);

  // Respiratory rate: average valid values
  int rrCount = 0;
  float rrValid[EPOCH_MAX_SAMPLES];
  for (int i = 0; i < count_; i++) {
    if (isValidMetric(rrValues_[i])) {
      rrValid[rrCount++] = rrValues_[i];
    }
  }
  stats.rrMean = arrMean(rrValid, rrCount);

  float confSum = 0;
  int fingerCount = 0;
  for (int i = 0; i < count_; i++) {
    confSum += confidenceValues_[i];
    if (fingerValues_[i]) fingerCount++;
  }
  stats.meanConfidence = count_ > 0 ? confSum / count_ : 0;
  stats.fingerDetectedPct = totalCount_ > 0 ? (100.0f * fingerCount) / totalCount_ : 0;
  stats.ready = true;

  reset();
  return stats;
}
