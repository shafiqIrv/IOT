#pragma once

#include "metrics_types.h"

struct EpochStats {
  float hrMean;
  float hrMin;
  float hrMax;
  float hrStd;
  float spo2Mean;
  float spo2Min;
  float spo2Max;
  float spo2Std;
  int spo2DesatCount;
  float hrvSdnn;
  float hrvRmssd;
  float hrvPnn50;
  float hrvMeanRR;
  float rrMean;
  int validSamples;
  int totalSamples;
  float meanConfidence;
  float fingerDetectedPct;
  unsigned long epochStartMs;
  unsigned long epochEndMs;
  bool ready;
};

class EpochAccumulator {
 public:
  void reset();
  void addMetrics(const SensorMetrics& metrics);
  bool isReady(unsigned long nowMs) const;
  EpochStats buildAndReset(unsigned long nowMs);

 private:
  float hrValues_[EPOCH_MAX_SAMPLES];
  float spo2Values_[EPOCH_MAX_SAMPLES];
  float rrValues_[EPOCH_MAX_SAMPLES];
  float confidenceValues_[EPOCH_MAX_SAMPLES];
  bool fingerValues_[EPOCH_MAX_SAMPLES];
  float hrvSdnn_[EPOCH_MAX_SAMPLES];
  float hrvRmssd_[EPOCH_MAX_SAMPLES];
  float hrvPnn50_[EPOCH_MAX_SAMPLES];
  float hrvMeanRR_[EPOCH_MAX_SAMPLES];
  int count_;
  int totalCount_;
  unsigned long epochStartMs_;
};
