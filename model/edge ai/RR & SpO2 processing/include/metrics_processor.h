#pragma once

#include "metrics_types.h"

bool isValidMetric(float value);

class MetricsProcessor {
 public:
  MetricsProcessor();

  void reset();
  SensorMetrics processSamples(const RawSample* samples, int count, unsigned long windowEndMs);
  int copyRecentRRIntervals(float* out, int maxCount) const;

  void calculateHRV(SensorMetrics& metrics) const;
  float calculateRespiratoryRate() const;
  int calculateSignalQuality(const SensorMetrics& metrics) const;

 private:
  void resetRRIntervals();
  void resetRespBuffer();
  void addRRInterval(float rrIntervalMs);
  void recordRecentRRInterval(float rrIntervalMs);
  int copyRRIntervals(float* out, int maxCount) const;
  void pushRespSample(double rawIR);
  static void fftInPlace(double* real, double* imag, int n);
  static float computeMedian(float* arr, int n);

  // HR state — PBA beat detection + median filter
  float rates_[HR_MEDIAN_RATE_SIZE];
  uint8_t rateSpot_;
  uint8_t rateCount_;
  float beatMedian_;
  unsigned long lastBeatMs_;
  unsigned long lastValidBeatMs_;

  // SpO2 state — Maxim library batch processing
  uint32_t irBuf_[SPO2_BUFFER_SIZE];
  uint32_t redBuf_[SPO2_BUFFER_SIZE];
  int spoBufIdx_;
  float currentSpo2_;

  // HRV state
  RRIntervalBuffer rrBuffer_;
  float recentRRIntervals_[RECENT_RR_INTERVALS_SIZE];
  int recentRRIntervalCount_;

  // Respiratory rate state
  double respBuffer_[RESP_DS_SIZE];
  int respIndex_;
  bool respBufferFull_;
  int respDecimationCount_;
  double respAccumulator_;
  unsigned long lastRespUpdateMs_;
  float currentRespiratoryRate_;

#ifndef ARDUINO
  // Native-only state for platform-independent beat detection and SpO2
  bool detectBeatNative(int32_t ir);
  void accumulateSpO2Native(int32_t ir, int32_t red);
  float computeSpO2Native();

  double nativeDcLevel_;
  int nativeWarmupCount_;
  double nativePrevAC_;
  int nativeRefractory_;

  struct NativeSpO2Acc {
    double avgRed;
    double avgIR;
    double sumRedRMS;
    double sumIRRMS;
    int sampleCount;
  };
  NativeSpO2Acc nativeSpO2Acc_;
#endif
};
