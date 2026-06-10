#include "metrics_processor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef ARDUINO
#include "heartRate.h"
#include "spo2_algorithm.h"
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool isValidMetric(float value) {
  return !std::isnan(value) && !std::isinf(value);
}

MetricsProcessor::MetricsProcessor() {
  reset();
}

void MetricsProcessor::reset() {
  std::memset(rates_, 0, sizeof(rates_));
  rateSpot_ = 0;
  rateCount_ = 0;
  beatMedian_ = NAN;
  lastBeatMs_ = 0;
  lastValidBeatMs_ = 0;

  std::memset(irBuf_, 0, sizeof(irBuf_));
  std::memset(redBuf_, 0, sizeof(redBuf_));
  spoBufIdx_ = 0;
  currentSpo2_ = NAN;

  std::memset(&rrBuffer_, 0, sizeof(rrBuffer_));
  std::memset(recentRRIntervals_, 0, sizeof(recentRRIntervals_));
  recentRRIntervalCount_ = 0;

  resetRespBuffer();

#ifndef ARDUINO
  nativeDcLevel_ = 0.0;
  nativeWarmupCount_ = 0;
  nativePrevAC_ = 0.0;
  nativeRefractory_ = 0;
  std::memset(&nativeSpO2Acc_, 0, sizeof(nativeSpO2Acc_));
#endif
}

void MetricsProcessor::resetRRIntervals() {
  rrBuffer_.count = 0;
  rrBuffer_.index = 0;
  lastBeatMs_ = 0;
  lastValidBeatMs_ = 0;
  rateSpot_ = 0;
  rateCount_ = 0;
  beatMedian_ = NAN;
}

void MetricsProcessor::resetRespBuffer() {
  std::memset(respBuffer_, 0, sizeof(respBuffer_));
  respIndex_ = 0;
  respBufferFull_ = false;
  respDecimationCount_ = 0;
  respAccumulator_ = 0.0;
  currentRespiratoryRate_ = NAN;
  lastRespUpdateMs_ = 0;
}

void MetricsProcessor::addRRInterval(float rrIntervalMs) {
  rrBuffer_.intervals[rrBuffer_.index] = rrIntervalMs;
  rrBuffer_.index = (rrBuffer_.index + 1) % RR_BUFFER_SIZE;
  if (rrBuffer_.count < RR_BUFFER_SIZE) {
    rrBuffer_.count++;
  }
}

void MetricsProcessor::recordRecentRRInterval(float rrIntervalMs) {
  if (recentRRIntervalCount_ < RECENT_RR_INTERVALS_SIZE) {
    recentRRIntervals_[recentRRIntervalCount_++] = rrIntervalMs;
    return;
  }

  for (int i = 1; i < RECENT_RR_INTERVALS_SIZE; i++) {
    recentRRIntervals_[i - 1] = recentRRIntervals_[i];
  }
  recentRRIntervals_[RECENT_RR_INTERVALS_SIZE - 1] = rrIntervalMs;
}

int MetricsProcessor::copyRRIntervals(float* out, int maxCount) const {
  int count = std::min(rrBuffer_.count, maxCount);
  int start = rrBuffer_.count == RR_BUFFER_SIZE ? rrBuffer_.index : 0;

  for (int i = 0; i < count; i++) {
    out[i] = rrBuffer_.intervals[(start + i) % RR_BUFFER_SIZE];
  }

  return count;
}

int MetricsProcessor::copyRecentRRIntervals(float* out, int maxCount) const {
  int count = std::min(recentRRIntervalCount_, maxCount);
  for (int i = 0; i < count; i++) {
    out[i] = recentRRIntervals_[i];
  }
  return count;
}

float MetricsProcessor::computeMedian(float* arr, int n) {
  if (n <= 0) {
    return NAN;
  }

  float tmp[HR_MEDIAN_RATE_SIZE];
  int safeN = std::min(n, (int)HR_MEDIAN_RATE_SIZE);
  for (int i = 0; i < safeN; i++) {
    tmp[i] = arr[i];
  }

  for (int i = 1; i < safeN; i++) {
    float key = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j] > key) {
      tmp[j + 1] = tmp[j];
      j--;
    }
    tmp[j + 1] = key;
  }

  return tmp[safeN / 2];
}

void MetricsProcessor::calculateHRV(SensorMetrics& metrics) const {
  if (rrBuffer_.count < HRV_MIN_INTERVALS) {
    metrics.meanRR = NAN;
    metrics.sdnn = NAN;
    metrics.rmssd = NAN;
    metrics.pnn50 = NAN;
    return;
  }

  float intervals[RR_BUFFER_SIZE];
  int count = copyRRIntervals(intervals, RR_BUFFER_SIZE);

  double sum = 0.0;
  for (int i = 0; i < count; i++) {
    sum += intervals[i];
  }
  double mean = sum / count;

  double varianceSum = 0.0;
  for (int i = 0; i < count; i++) {
    double diff = intervals[i] - mean;
    varianceSum += diff * diff;
  }

  double successiveDiffSqSum = 0.0;
  int pnn50Count = 0;
  for (int i = 1; i < count; i++) {
    double diff = intervals[i] - intervals[i - 1];
    successiveDiffSqSum += diff * diff;
    if (std::fabs(diff) > 50.0) {
      pnn50Count++;
    }
  }

  metrics.meanRR = (float)mean;
  metrics.sdnn = (float)std::sqrt(varianceSum / count);
  metrics.rmssd = (float)std::sqrt(successiveDiffSqSum / (count - 1));
  metrics.pnn50 = (float)((100.0 * pnn50Count) / (count - 1));
}

void MetricsProcessor::pushRespSample(double rawIR) {
  respAccumulator_ += rawIR;
  respDecimationCount_++;

  if (respDecimationCount_ < RESP_DECIMATION) {
    return;
  }

  double downsampled = respAccumulator_ / respDecimationCount_;
  respAccumulator_ = 0.0;
  respDecimationCount_ = 0;

  respBuffer_[respIndex_] = downsampled;
  respIndex_ = (respIndex_ + 1) % RESP_DS_SIZE;
  if (respIndex_ == 0) {
    respBufferFull_ = true;
  }
}

void MetricsProcessor::fftInPlace(double* real, double* imag, int n) {
  int j = 0;
  for (int i = 1; i < n; i++) {
    int bit = n >> 1;
    while (j & bit) {
      j ^= bit;
      bit >>= 1;
    }
    j ^= bit;

    if (i < j) {
      std::swap(real[i], real[j]);
      std::swap(imag[i], imag[j]);
    }
  }

  for (int len = 2; len <= n; len <<= 1) {
    double angle = -2.0 * M_PI / len;
    double wLenReal = std::cos(angle);
    double wLenImag = std::sin(angle);

    for (int i = 0; i < n; i += len) {
      double wReal = 1.0;
      double wImag = 0.0;

      for (int k = 0; k < len / 2; k++) {
        int evenIndex = i + k;
        int oddIndex = i + k + len / 2;

        double oddReal = real[oddIndex] * wReal - imag[oddIndex] * wImag;
        double oddImag = real[oddIndex] * wImag + imag[oddIndex] * wReal;

        real[oddIndex] = real[evenIndex] - oddReal;
        imag[oddIndex] = imag[evenIndex] - oddImag;
        real[evenIndex] += oddReal;
        imag[evenIndex] += oddImag;

        double nextWReal = wReal * wLenReal - wImag * wLenImag;
        wImag = wReal * wLenImag + wImag * wLenReal;
        wReal = nextWReal;
      }
    }
  }
}

float MetricsProcessor::calculateRespiratoryRate() const {
  if (!respBufferFull_) {
    return NAN;
  }

  // Large FFT working buffers are kept in static storage instead of on the
  // stack: ~10.6 KB of doubles would overflow the processing task's stack and
  // corrupt memory (intermittent crashes + RespRate stuck at NaN). This
  // function is only ever called from the single Core-1 processing task, so
  // static (non-reentrant) buffers are safe.
  static double signal[RESP_DS_SIZE];
  for (int i = 0; i < RESP_DS_SIZE; i++) {
    signal[i] = respBuffer_[(respIndex_ + i) % RESP_DS_SIZE];
  }

  double mean = 0.0;
  for (int i = 0; i < RESP_DS_SIZE; i++) {
    mean += signal[i];
  }
  mean /= RESP_DS_SIZE;

  static double real[RESP_FFT_SIZE];
  static double imag[RESP_FFT_SIZE];
  double signalPower = 0.0;
  for (int i = 0; i < RESP_FFT_SIZE; i++) {
    if (i < RESP_DS_SIZE) {
      double window = 0.5 * (1.0 - std::cos((2.0 * M_PI * i) / (RESP_DS_SIZE - 1)));
      real[i] = (signal[i] - mean) * window;
      signalPower += real[i] * real[i];
    } else {
      real[i] = 0.0;
    }
    imag[i] = 0.0;
  }

  if (signalPower < 1.0) {
    return NAN;
  }

  fftInPlace(real, imag, RESP_FFT_SIZE);

  int minBin = (int)std::ceil(0.1 * RESP_FFT_SIZE / RESP_DS_RATE);
  int maxBin = (int)std::floor(0.5 * RESP_FFT_SIZE / RESP_DS_RATE);
  double peakPower = 0.0;
  double bandPower = 0.0;
  int peakBin = -1;

  for (int bin = minBin; bin <= maxBin; bin++) {
    double power = real[bin] * real[bin] + imag[bin] * imag[bin];
    bandPower += power;
    if (power > peakPower) {
      peakPower = power;
      peakBin = bin;
    }
  }

  if (peakBin < 0 || bandPower <= 0.0) {
    return NAN;
  }

  double peakRatio = peakPower / (bandPower / (maxBin - minBin + 1));
  double dominantFrequency = ((double)peakBin * RESP_DS_RATE) / RESP_FFT_SIZE;
  if (dominantFrequency < 0.1 || dominantFrequency > 0.5 || peakRatio < 1.4) {
    return NAN;
  }

  return (float)(dominantFrequency * 60.0);
}

int MetricsProcessor::calculateSignalQuality(const SensorMetrics& metrics) const {
  if (!metrics.fingerDetected) {
    return 0;
  }

  int score = 0;
  if (rateCount_ >= 3 && isValidMetric(beatMedian_) && beatMedian_ > 0.0f) {
    score++;
  }
  if (isValidMetric(currentSpo2_)) {
    score++;
  }
  if (rrBuffer_.count >= 3) {
    score++;
  }

  if (score <= 1) {
    return 1;
  }
  if (score == 2) {
    return 2;
  }
  return 3;
}

// ---- Native-only implementations (not compiled on Arduino) ----
#ifndef ARDUINO

bool MetricsProcessor::detectBeatNative(int32_t ir) {
  // IIR high-pass: fast α during warmup, then slow α to remove DC + respiration.
  // At α=0.001 (τ≈1000 samples=10s), 0.25Hz respiration attenuates to ~6% → ±269 units.
  // Beat dip in test signal ≈ −2600 units, threshold −500 → clean separation.
  nativeWarmupCount_++;
  double alpha = (nativeWarmupCount_ < 200) ? 0.05 : 0.001;

  if (nativeWarmupCount_ == 1) {
    nativeDcLevel_ = (double)ir;
  } else {
    nativeDcLevel_ = nativeDcLevel_ * (1.0 - alpha) + (double)ir * alpha;
  }

  if (nativeWarmupCount_ < 200) {
    nativePrevAC_ = (double)ir - nativeDcLevel_;
    return false;
  }

  double ac = (double)ir - nativeDcLevel_;
  bool beat = false;

  if (nativeRefractory_ > 0) {
    nativeRefractory_--;
  } else if (nativePrevAC_ < -500.0 && ac > nativePrevAC_) {
    beat = true;
    nativeRefractory_ = 40;
  }

  nativePrevAC_ = ac;
  return beat;
}

void MetricsProcessor::accumulateSpO2Native(int32_t ir, int32_t red) {
  if (nativeSpO2Acc_.avgIR <= 0.0) {
    nativeSpO2Acc_.avgRed = (double)red;
    nativeSpO2Acc_.avgIR = (double)ir;
  } else {
    nativeSpO2Acc_.avgRed = 0.95 * nativeSpO2Acc_.avgRed + 0.05 * (double)red;
    nativeSpO2Acc_.avgIR = 0.95 * nativeSpO2Acc_.avgIR + 0.05 * (double)ir;
  }

  double acRed = (double)red - nativeSpO2Acc_.avgRed;
  double acIR = (double)ir - nativeSpO2Acc_.avgIR;
  nativeSpO2Acc_.sumRedRMS += acRed * acRed;
  nativeSpO2Acc_.sumIRRMS += acIR * acIR;
  nativeSpO2Acc_.sampleCount++;
}

float MetricsProcessor::computeSpO2Native() {
  if (nativeSpO2Acc_.sampleCount <= 0 ||
      nativeSpO2Acc_.avgRed <= 0.0 ||
      nativeSpO2Acc_.avgIR <= 0.0 ||
      nativeSpO2Acc_.sumIRRMS <= 0.0) {
    return NAN;
  }

  double rmsRed = std::sqrt(nativeSpO2Acc_.sumRedRMS / nativeSpO2Acc_.sampleCount);
  double rmsIR = std::sqrt(nativeSpO2Acc_.sumIRRMS / nativeSpO2Acc_.sampleCount);
  if (rmsIR <= 0.0) {
    return NAN;
  }

  double r = (rmsRed / nativeSpO2Acc_.avgRed) / (rmsIR / nativeSpO2Acc_.avgIR);
  double spo2 = 104.0 - 17.0 * r;

  if (spo2 < 70.0 || spo2 > 100.0) {
    return NAN;
  }

  return (float)spo2;
}

#endif  // !ARDUINO

// ---- Main sample processing ----

SensorMetrics MetricsProcessor::processSamples(const RawSample* samples, int count, unsigned long windowEndMs) {
  recentRRIntervalCount_ = 0;
  bool anyFingerDetected = false;

#ifndef ARDUINO
  std::memset(&nativeSpO2Acc_, 0, sizeof(nativeSpO2Acc_));
#endif

  for (int i = 0; i < count; i++) {
    bool fingerDetected = (long)samples[i].ir > IR_FINGER_THRESHOLD;

    if (!fingerDetected) {
      continue;
    }
    anyFingerDetected = true;

    unsigned long sampleOffsetMs = (unsigned long)((count - 1 - i) * SAMPLE_PERIOD_MS);
    unsigned long sampleTimeMs = windowEndMs > sampleOffsetMs ? windowEndMs - sampleOffsetMs : windowEndMs;

    // HR: detect beats via PBA algorithm (SparkFun) or native IIR trough detector
    bool beat = false;
#ifdef ARDUINO
    beat = checkForBeat((long)samples[i].ir);
#else
    beat = detectBeatNative(samples[i].ir);
#endif

    if (beat) {
      if (lastBeatMs_ != 0) {
        float rrInterval = (float)(sampleTimeMs - lastBeatMs_);
        if (rrInterval >= RR_MIN_MS && rrInterval <= RR_MAX_MS) {
          addRRInterval(rrInterval);
          recordRecentRRInterval(rrInterval);

          float bpm = 60000.0f / rrInterval;
          if (bpm >= 20.0f && bpm <= 255.0f) {
            rates_[rateSpot_] = bpm;
            rateSpot_ = (rateSpot_ + 1) % HR_MEDIAN_RATE_SIZE;
            if (rateCount_ < HR_MEDIAN_RATE_SIZE) {
              rateCount_++;
            }
            beatMedian_ = computeMedian(rates_, rateCount_);
          }
        }
      }

      lastBeatMs_ = sampleTimeMs;
      lastValidBeatMs_ = sampleTimeMs;
    }

    // SpO2: accumulate into Maxim 100-sample batch buffer (Arduino) or IIR accumulator (native)
#ifdef ARDUINO
    irBuf_[spoBufIdx_] = (uint32_t)samples[i].ir;
    redBuf_[spoBufIdx_] = (uint32_t)samples[i].red;
    spoBufIdx_++;

    if (spoBufIdx_ >= SPO2_BUFFER_SIZE) {
      int32_t spo2Raw = 0;
      int8_t validSpo2 = 0;
      int32_t hrIgnored = 0;
      int8_t hrValid = 0;
      maxim_heart_rate_and_oxygen_saturation(
        irBuf_, SPO2_BUFFER_SIZE, redBuf_,
        &spo2Raw, &validSpo2,
        &hrIgnored, &hrValid
      );

      if (validSpo2 && spo2Raw > 0 && spo2Raw <= 100) {
        currentSpo2_ = (float)spo2Raw;
      } else {
        currentSpo2_ = NAN;
      }

      // Slide buffer: keep last SPO2_BUFFER_OVERLAP samples
      for (int k = 0; k < SPO2_BUFFER_OVERLAP; k++) {
        irBuf_[k] = irBuf_[SPO2_BUFFER_SIZE - SPO2_BUFFER_OVERLAP + k];
        redBuf_[k] = redBuf_[SPO2_BUFFER_SIZE - SPO2_BUFFER_OVERLAP + k];
      }
      spoBufIdx_ = SPO2_BUFFER_OVERLAP;
    }
#else
    accumulateSpO2Native(samples[i].ir, samples[i].red);
#endif

    pushRespSample((double)samples[i].ir);
  }

  if (!anyFingerDetected) {
    resetRRIntervals();
    resetRespBuffer();
    currentSpo2_ = NAN;
    spoBufIdx_ = 0;
#ifndef ARDUINO
    std::memset(&nativeSpO2Acc_, 0, sizeof(nativeSpO2Acc_));
#endif

    SensorMetrics metrics;
    metrics.bpm = NAN;
    metrics.spo2 = NAN;
    metrics.respiratoryRate = NAN;
    metrics.meanRR = NAN;
    metrics.sdnn = NAN;
    metrics.rmssd = NAN;
    metrics.pnn50 = NAN;
    metrics.confidence = 0;
    metrics.fingerDetected = false;
    metrics.timestamp = windowEndMs;
    return metrics;
  }

  // Suppress HR if no beat detected recently (stale)
  if (lastValidBeatMs_ == 0 || windowEndMs - lastValidBeatMs_ > 4000UL) {
    beatMedian_ = NAN;
    rateCount_ = 0;
  }

#ifndef ARDUINO
  currentSpo2_ = computeSpO2Native();
#endif

  SensorMetrics metrics;
  metrics.bpm = beatMedian_;
  metrics.spo2 = currentSpo2_;
  metrics.fingerDetected = true;
  metrics.timestamp = windowEndMs;

  calculateHRV(metrics);

  if (windowEndMs - lastRespUpdateMs_ >= RESP_UPDATE_INTERVAL_MS) {
    currentRespiratoryRate_ = calculateRespiratoryRate();
    lastRespUpdateMs_ = windowEndMs;
  }
  metrics.respiratoryRate = currentRespiratoryRate_;

  metrics.confidence = calculateSignalQuality(metrics);

  if (metrics.confidence <= 1) {
    metrics.respiratoryRate = NAN;
    metrics.meanRR = NAN;
    metrics.sdnn = NAN;
    metrics.rmssd = NAN;
    metrics.pnn50 = NAN;
  }

  return metrics;
}
