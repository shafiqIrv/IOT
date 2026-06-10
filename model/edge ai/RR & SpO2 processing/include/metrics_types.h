#pragma once

#include <cstdint>

constexpr int SAMPLE_RATE = 100;
constexpr int SAMPLE_PERIOD_MS = 1000 / SAMPLE_RATE;
constexpr int PROCESS_WINDOW_SEC = 1;
constexpr int PROCESS_BUFFER_SIZE = SAMPLE_RATE * PROCESS_WINDOW_SEC;

constexpr int RR_BUFFER_SIZE = 64;
constexpr int RECENT_RR_INTERVALS_SIZE = 32;
constexpr float RR_MIN_MS = 300.0f;
constexpr float RR_MAX_MS = 2000.0f;
constexpr int HRV_MIN_INTERVALS = 10;

constexpr int HR_MEDIAN_RATE_SIZE = 9;

constexpr int SPO2_BUFFER_SIZE = 100;
constexpr int SPO2_BUFFER_OVERLAP = 25;

constexpr int RESP_DS_RATE = 10;
constexpr int RESP_DECIMATION = SAMPLE_RATE / RESP_DS_RATE;
constexpr int RESP_DS_WINDOW_SEC = 30;
constexpr int RESP_DS_SIZE = RESP_DS_RATE * RESP_DS_WINDOW_SEC;
constexpr int RESP_FFT_SIZE = 512;
constexpr unsigned long RESP_UPDATE_INTERVAL_MS = 5000UL;

constexpr long IR_FINGER_THRESHOLD = 50000;
constexpr const char* DEVICE_ID = "WEARABLE-001";
constexpr const char* TOPIC_LIVE = "sensor/wearable-001/live";
constexpr const char* TOPIC_EPOCH = "sensor/wearable-001/epoch";
constexpr const char* TOPIC_APNEA = "sensor/wearable-001/inference/apnea";
constexpr int EPOCH_WINDOW_SEC = 60;
constexpr int EPOCH_MAX_SAMPLES = 120;

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
