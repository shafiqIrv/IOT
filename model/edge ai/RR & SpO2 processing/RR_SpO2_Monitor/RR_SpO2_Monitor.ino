/*
 * Standalone Arduino IDE sketch for ESP32-S3 + MAX30102/MAX30105.
 *
 * Upload this folder directly from Arduino IDE. It prints HR, SpO2,
 * respiratory rate, HRV/PRV metrics, signal confidence, and finger status to
 * Serial Monitor. No WiFi, MQTT, or ML inference is included.
 */

#include <Arduino.h>
#include <Wire.h>
#include <MAX30105.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

constexpr int SAMPLE_RATE = 100;
constexpr int SAMPLE_PERIOD_MS = 1000 / SAMPLE_RATE;
constexpr int PROCESS_WINDOW_SEC = 5;
constexpr int PROCESS_BUFFER_SIZE = SAMPLE_RATE * PROCESS_WINDOW_SEC;

constexpr int RR_BUFFER_SIZE = 64;
constexpr int RECENT_RR_INTERVALS_SIZE = 32;
constexpr float RR_MIN_MS = 300.0f;
constexpr float RR_MAX_MS = 2000.0f;
constexpr int HRV_MIN_INTERVALS = 10;

constexpr int SPO2_AVG_SIZE = 5;

constexpr int RESP_DS_RATE = 10;
constexpr int RESP_DECIMATION = SAMPLE_RATE / RESP_DS_RATE;
constexpr int RESP_DS_WINDOW_SEC = 30;
constexpr int RESP_DS_SIZE = RESP_DS_RATE * RESP_DS_WINDOW_SEC;
constexpr int RESP_FFT_SIZE = 512;
constexpr unsigned long RESP_UPDATE_INTERVAL_MS = 5000UL;

constexpr long IR_FINGER_THRESHOLD = 80000;

struct SensorMetrics {
  float bpm;
  float spo2;
  float respiratoryRate;
  float meanRR;
  float sdnn;
  float rmssd;
  float pnn50;
  int confidence;
  bool fingerDetected;
  unsigned long timestamp;
};

struct RawSample {
  int32_t ir;
  int32_t red;
};

struct RRIntervalBuffer {
  float intervals[RR_BUFFER_SIZE];
  int count;
  int index;
};

bool isValidMetric(float value);
void printMetricValue(float value, int decimals);
void printMetrics(const SensorMetrics& metrics);

class MetricsProcessor {
 public:
  MetricsProcessor();

  void reset();
  SensorMetrics processSamples(const RawSample* samples, int count, unsigned long windowEndMs);

 private:
  struct SpO2Accumulator {
    double avgRed;
    double avgIR;
    double sumRedRMS;
    double sumIRRMS;
    int sampleCount;
    bool fingerDetected;
    double filteredAmplitude;
  };

  SensorMetrics makeInvalidMetrics(bool fingerDetected, unsigned long timestamp) const;
  void resetRRIntervals();
  void resetSpO2Smoothing();
  void resetRespBuffer();
  void addRRInterval(float rrIntervalMs);
  int copyRRIntervals(float* out, int maxCount) const;
  void calculateHRV(SensorMetrics& metrics) const;
  float smoothSpO2(float newSpO2);
  float calculateSpO2();
  int32_t filterIR(int32_t rawIR);
  void pushRespSample(double rawIR);
  int processHeartRate(int count, SensorMetrics& metrics, unsigned long windowEndMs);
  float calculateRespiratoryRate();
  float calculateStdDev(const int32_t* samples, int count, double* meanOut) const;
  int calculateSignalQuality(bool fingerDetected,
                             float filteredAmplitude,
                             int validPeaks,
                             const SensorMetrics& metrics) const;
  static void fftInPlace(double* real, double* imag, int n);

  SpO2Accumulator spo2Accumulator_;
  RRIntervalBuffer rrBuffer_;
  float spo2Buffer_[SPO2_AVG_SIZE];
  int spo2Index_;
  int spo2Count_;

  double respBuffer_[RESP_DS_SIZE];
  int respIndex_;
  bool respBufferFull_;
  int respDecimationCount_;
  double respAccumulator_;

  float hpX_[2];
  float hpY_[2];
  float filteredIR_;

  unsigned long lastPeakTimeMs_;
  unsigned long lastValidBeatMs_;
  unsigned long lastRespUpdateMs_;
  float currentBpm_;
  float currentRespiratoryRate_;

  int32_t filteredSamples_[PROCESS_BUFFER_SIZE];
  double respSignal_[RESP_DS_SIZE];
  double fftReal_[RESP_FFT_SIZE];
  double fftImag_[RESP_FFT_SIZE];
};

MAX30105 particleSensor;
MetricsProcessor metricsProcessor;
RawSample sampleBuffer[PROCESS_BUFFER_SIZE];
int sampleBufferIndex = 0;

bool isValidMetric(float value) {
  return !isnan(value) && !isinf(value);
}

MetricsProcessor::MetricsProcessor() {
  reset();
}

void MetricsProcessor::reset() {
  memset(&spo2Accumulator_, 0, sizeof(spo2Accumulator_));
  memset(&rrBuffer_, 0, sizeof(rrBuffer_));
  memset(respBuffer_, 0, sizeof(respBuffer_));
  memset(filteredSamples_, 0, sizeof(filteredSamples_));
  memset(respSignal_, 0, sizeof(respSignal_));
  memset(fftReal_, 0, sizeof(fftReal_));
  memset(fftImag_, 0, sizeof(fftImag_));
  hpX_[0] = 0.0f;
  hpX_[1] = 0.0f;
  hpY_[0] = 0.0f;
  hpY_[1] = 0.0f;
  filteredIR_ = 0.0f;
  respIndex_ = 0;
  respBufferFull_ = false;
  respDecimationCount_ = 0;
  respAccumulator_ = 0.0;
  lastPeakTimeMs_ = 0;
  lastValidBeatMs_ = 0;
  lastRespUpdateMs_ = 0;
  currentBpm_ = NAN;
  currentRespiratoryRate_ = NAN;
  resetSpO2Smoothing();
}

SensorMetrics MetricsProcessor::makeInvalidMetrics(bool fingerDetected, unsigned long timestamp) const {
  SensorMetrics metrics;
  metrics.bpm = NAN;
  metrics.spo2 = NAN;
  metrics.respiratoryRate = NAN;
  metrics.meanRR = NAN;
  metrics.sdnn = NAN;
  metrics.rmssd = NAN;
  metrics.pnn50 = NAN;
  metrics.confidence = fingerDetected ? 1 : 0;
  metrics.fingerDetected = fingerDetected;
  metrics.timestamp = timestamp;
  return metrics;
}

void MetricsProcessor::resetRRIntervals() {
  rrBuffer_.count = 0;
  rrBuffer_.index = 0;
  lastPeakTimeMs_ = 0;
  lastValidBeatMs_ = 0;
  currentBpm_ = NAN;
}

void MetricsProcessor::resetSpO2Smoothing() {
  for (int i = 0; i < SPO2_AVG_SIZE; i++) {
    spo2Buffer_[i] = NAN;
  }
  spo2Index_ = 0;
  spo2Count_ = 0;
}

void MetricsProcessor::resetRespBuffer() {
  memset(respBuffer_, 0, sizeof(respBuffer_));
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

int MetricsProcessor::copyRRIntervals(float* out, int maxCount) const {
  int count = rrBuffer_.count < maxCount ? rrBuffer_.count : maxCount;
  int start = rrBuffer_.count == RR_BUFFER_SIZE ? rrBuffer_.index : 0;

  for (int i = 0; i < count; i++) {
    out[i] = rrBuffer_.intervals[(start + i) % RR_BUFFER_SIZE];
  }

  return count;
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
    if (fabs(diff) > 50.0) {
      pnn50Count++;
    }
  }

  metrics.meanRR = (float)mean;
  metrics.sdnn = (float)sqrt(varianceSum / count);
  metrics.rmssd = (float)sqrt(successiveDiffSqSum / (count - 1));
  metrics.pnn50 = (float)((100.0 * pnn50Count) / (count - 1));
}

float MetricsProcessor::smoothSpO2(float newSpO2) {
  if (!isValidMetric(newSpO2)) {
    return NAN;
  }

  spo2Buffer_[spo2Index_] = newSpO2;
  spo2Index_ = (spo2Index_ + 1) % SPO2_AVG_SIZE;
  if (spo2Count_ < SPO2_AVG_SIZE) {
    spo2Count_++;
  }

  double sum = 0.0;
  int validCount = 0;
  for (int i = 0; i < spo2Count_; i++) {
    if (isValidMetric(spo2Buffer_[i])) {
      sum += spo2Buffer_[i];
      validCount++;
    }
  }

  return validCount > 0 ? (float)(sum / validCount) : NAN;
}

float MetricsProcessor::calculateSpO2() {
  if (!spo2Accumulator_.fingerDetected ||
      spo2Accumulator_.sampleCount <= 0 ||
      spo2Accumulator_.avgRed <= 0.0 ||
      spo2Accumulator_.avgIR <= 0.0 ||
      spo2Accumulator_.sumIRRMS <= 0.0) {
    return NAN;
  }

  double rmsRed = sqrt(spo2Accumulator_.sumRedRMS / spo2Accumulator_.sampleCount);
  double rmsIR = sqrt(spo2Accumulator_.sumIRRMS / spo2Accumulator_.sampleCount);
  if (rmsIR <= 0.0) {
    return NAN;
  }

  double redRatio = rmsRed / spo2Accumulator_.avgRed;
  double irRatio = rmsIR / spo2Accumulator_.avgIR;
  if (irRatio <= 0.0) {
    return NAN;
  }

  double r = redRatio / irRatio;
  double spo2 = 104.0 - (17.0 * r);
  if (spo2 < 0.0) {
    spo2 = 0.0;
  }
  if (spo2 > 100.0) {
    spo2 = 100.0;
  }

  if (spo2 < 70.0 || spo2 > 100.0) {
    return NAN;
  }

  return smoothSpO2((float)spo2);
}

int32_t MetricsProcessor::filterIR(int32_t rawIR) {
  hpX_[0] = hpX_[1];
  hpX_[1] = (float)rawIR;
  hpY_[0] = hpY_[1];
  hpY_[1] = (hpX_[1] - hpX_[0]) + (0.969f * hpY_[0]);
  filteredIR_ = (0.7f * -hpY_[1]) + (0.3f * filteredIR_);
  return (int32_t)filteredIR_;
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
      double tmpReal = real[i];
      real[i] = real[j];
      real[j] = tmpReal;
      double tmpImag = imag[i];
      imag[i] = imag[j];
      imag[j] = tmpImag;
    }
  }

  for (int len = 2; len <= n; len <<= 1) {
    double angle = -2.0 * M_PI / len;
    double wLenReal = cos(angle);
    double wLenImag = sin(angle);

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

float MetricsProcessor::calculateRespiratoryRate() {
  if (!respBufferFull_) {
    return NAN;
  }

  for (int i = 0; i < RESP_DS_SIZE; i++) {
    respSignal_[i] = respBuffer_[(respIndex_ + i) % RESP_DS_SIZE];
  }

  double mean = 0.0;
  for (int i = 0; i < RESP_DS_SIZE; i++) {
    mean += respSignal_[i];
  }
  mean /= RESP_DS_SIZE;

  double signalPower = 0.0;
  for (int i = 0; i < RESP_FFT_SIZE; i++) {
    if (i < RESP_DS_SIZE) {
      double window = 0.5 * (1.0 - cos((2.0 * M_PI * i) / (RESP_DS_SIZE - 1)));
      fftReal_[i] = (respSignal_[i] - mean) * window;
      signalPower += fftReal_[i] * fftReal_[i];
    } else {
      fftReal_[i] = 0.0;
    }
    fftImag_[i] = 0.0;
  }

  if (signalPower < 1.0) {
    return NAN;
  }

  fftInPlace(fftReal_, fftImag_, RESP_FFT_SIZE);

  int minBin = (int)ceil(0.1 * RESP_FFT_SIZE / RESP_DS_RATE);
  int maxBin = (int)floor(0.5 * RESP_FFT_SIZE / RESP_DS_RATE);
  double peakPower = 0.0;
  double bandPower = 0.0;
  int peakBin = -1;

  for (int bin = minBin; bin <= maxBin; bin++) {
    double power = fftReal_[bin] * fftReal_[bin] + fftImag_[bin] * fftImag_[bin];
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

float MetricsProcessor::calculateStdDev(const int32_t* samples, int count, double* meanOut) const {
  double mean = 0.0;
  for (int i = 0; i < count; i++) {
    mean += samples[i];
  }
  mean /= count;

  double varianceSum = 0.0;
  for (int i = 0; i < count; i++) {
    double diff = samples[i] - mean;
    varianceSum += diff * diff;
  }

  if (meanOut != nullptr) {
    *meanOut = mean;
  }

  return (float)sqrt(varianceSum / count);
}

int MetricsProcessor::processHeartRate(int count, SensorMetrics& metrics, unsigned long windowEndMs) {
  double mean = 0.0;
  float amplitude = calculateStdDev(filteredSamples_, count, &mean);
  double threshold = mean + (40.0f > amplitude * 0.55f ? 40.0f : amplitude * 0.55f);
  int validPeaks = 0;

  for (int i = 1; i < count - 1; i++) {
    bool localMax = filteredSamples_[i] > filteredSamples_[i - 1] && filteredSamples_[i] >= filteredSamples_[i + 1];
    if (!localMax || filteredSamples_[i] < threshold) {
      continue;
    }

    unsigned long sampleOffsetMs = (unsigned long)((count - 1 - i) * SAMPLE_PERIOD_MS);
    unsigned long peakTimeMs = windowEndMs > sampleOffsetMs ? windowEndMs - sampleOffsetMs : windowEndMs;

    if (lastPeakTimeMs_ == 0) {
      lastPeakTimeMs_ = peakTimeMs;
      continue;
    }

    float rrInterval = (float)(peakTimeMs - lastPeakTimeMs_);
    if (rrInterval < RR_MIN_MS) {
      continue;
    }

    lastPeakTimeMs_ = peakTimeMs;
    if (rrInterval > RR_MAX_MS) {
      continue;
    }

    addRRInterval(rrInterval);
    float instantBpm = 60000.0f / rrInterval;
    currentBpm_ = isValidMetric(currentBpm_) ? (0.75f * currentBpm_ + 0.25f * instantBpm) : instantBpm;
    lastValidBeatMs_ = peakTimeMs;
    validPeaks++;
  }

  if (lastValidBeatMs_ == 0 || windowEndMs - lastValidBeatMs_ > 4000UL) {
    currentBpm_ = NAN;
  }

  metrics.bpm = currentBpm_;
  return validPeaks;
}

int MetricsProcessor::calculateSignalQuality(bool fingerDetected,
                                             float filteredAmplitude,
                                             int validPeaks,
                                             const SensorMetrics& metrics) const {
  if (!fingerDetected) {
    return 0;
  }

  int score = 0;
  if (filteredAmplitude > 40.0f) {
    score++;
  }
  if (validPeaks > 0 && isValidMetric(metrics.bpm)) {
    score++;
  }
  if (rrBuffer_.count >= 3 || isValidMetric(metrics.spo2)) {
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

SensorMetrics MetricsProcessor::processSamples(const RawSample* samples, int count, unsigned long windowEndMs) {
  memset(&spo2Accumulator_, 0, sizeof(spo2Accumulator_));

  for (int i = 0; i < count; i++) {
    bool fingerDetected = samples[i].ir > IR_FINGER_THRESHOLD;
    spo2Accumulator_.fingerDetected = spo2Accumulator_.fingerDetected || fingerDetected;

    int32_t finalValue = filterIR(samples[i].ir);
    filteredSamples_[i] = finalValue;

    if (fingerDetected) {
      if (spo2Accumulator_.avgRed <= 0.0) {
        spo2Accumulator_.avgRed = samples[i].red;
        spo2Accumulator_.avgIR = samples[i].ir;
      } else {
        spo2Accumulator_.avgRed = (0.95 * spo2Accumulator_.avgRed) + (0.05 * (double)samples[i].red);
        spo2Accumulator_.avgIR = (0.95 * spo2Accumulator_.avgIR) + (0.05 * (double)samples[i].ir);
      }

      double acRed = (double)samples[i].red - spo2Accumulator_.avgRed;
      double acIR = (double)samples[i].ir - spo2Accumulator_.avgIR;
      spo2Accumulator_.sumRedRMS += acRed * acRed;
      spo2Accumulator_.sumIRRMS += acIR * acIR;
      spo2Accumulator_.sampleCount++;
      double absFiltered = fabs((double)finalValue);
      if (absFiltered > spo2Accumulator_.filteredAmplitude) {
        spo2Accumulator_.filteredAmplitude = absFiltered;
      }
      pushRespSample(samples[i].ir);
    }
  }

  SensorMetrics metrics = makeInvalidMetrics(spo2Accumulator_.fingerDetected, windowEndMs);
  if (!spo2Accumulator_.fingerDetected) {
    resetRRIntervals();
    resetSpO2Smoothing();
    resetRespBuffer();
    return metrics;
  }

  int validPeaks = processHeartRate(count, metrics, windowEndMs);
  metrics.spo2 = calculateSpO2();
  calculateHRV(metrics);

  if (windowEndMs - lastRespUpdateMs_ >= RESP_UPDATE_INTERVAL_MS) {
    currentRespiratoryRate_ = calculateRespiratoryRate();
    lastRespUpdateMs_ = windowEndMs;
  }
  metrics.respiratoryRate = currentRespiratoryRate_;

  metrics.confidence = calculateSignalQuality(
    spo2Accumulator_.fingerDetected,
    (float)spo2Accumulator_.filteredAmplitude,
    validPeaks,
    metrics
  );

  if (metrics.confidence <= 1) {
    metrics.respiratoryRate = NAN;
    metrics.meanRR = NAN;
    metrics.sdnn = NAN;
    metrics.rmssd = NAN;
    metrics.pnn50 = NAN;
  }

  return metrics;
}

void printMetricValue(float value, int decimals) {
  if (isValidMetric(value)) {
    Serial.print(value, decimals);
  } else {
    Serial.print("--");
  }
}

void printMetrics(const SensorMetrics& metrics) {
  Serial.print("t: ");
  Serial.print(metrics.timestamp / 1000.0f, 1);
  Serial.print("s | HR: ");
  printMetricValue(metrics.bpm, 1);
  Serial.print(" bpm | SpO2: ");
  printMetricValue(metrics.spo2, 1);
  Serial.print(" % | RespRate: ");
  printMetricValue(metrics.respiratoryRate, 1);
  Serial.print(" br/min | meanRR: ");
  printMetricValue(metrics.meanRR, 1);
  Serial.print(" ms | SDNN: ");
  printMetricValue(metrics.sdnn, 1);
  Serial.print(" ms | RMSSD: ");
  printMetricValue(metrics.rmssd, 1);
  Serial.print(" ms | pNN50: ");
  printMetricValue(metrics.pnn50, 1);
  Serial.print(" % | confidence: ");
  Serial.print(metrics.confidence);
  Serial.print(" | fingerDetected: ");
  Serial.println(metrics.fingerDetected ? "true" : "false");
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("RR + SpO2 Monitor");
  Serial.println("Initializing MAX3010x sensor...");

  Wire.begin();
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX3010x not found. Check wiring, power, and I2C pins.");
    while (true) {
      delay(1000);
    }
  }

  particleSensor.setup(150, 4, 2, SAMPLE_RATE, 411, 16384);
  particleSensor.clearFIFO();

  metricsProcessor.reset();
  sampleBufferIndex = 0;

  Serial.println("Sensor detected.");
  Serial.println("Keep your finger steady on the sensor.");
  Serial.println("HR and SpO2 update every 5 seconds; respiratory rate needs about 30 seconds.");
  Serial.println("Open Serial Monitor at 115200 baud.");
  Serial.println();
}

void loop() {
  particleSensor.check();

  while (particleSensor.available()) {
    sampleBuffer[sampleBufferIndex].ir = particleSensor.getFIFOIR();
    sampleBuffer[sampleBufferIndex].red = particleSensor.getFIFORed();
    particleSensor.nextSample();

    sampleBufferIndex++;
    if (sampleBufferIndex >= PROCESS_BUFFER_SIZE) {
      SensorMetrics metrics = metricsProcessor.processSamples(sampleBuffer, PROCESS_BUFFER_SIZE, millis());
      printMetrics(metrics);
      sampleBufferIndex = 0;
    }
  }

  delay(5);
}
