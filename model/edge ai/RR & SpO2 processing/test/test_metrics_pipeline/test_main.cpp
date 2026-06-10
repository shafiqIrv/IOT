#include <unity.h>

#include <cmath>
#include <cstring>
#include <string>

#include "metrics_payload.h"
#include "metrics_processor.h"
#include "metrics_types.h"

static constexpr double TEST_PI = 3.14159265358979323846;

void setUp() {}

void tearDown() {}

static RawSample makeSyntheticSample(double t,
                                     double heartRateBpm = 75.0,
                                     double respiratoryHz = 0.25,
                                     bool fingerOn = true,
                                     double pulseScale = 1.0) {
  if (!fingerOn) {
    return RawSample{20000, 18000};
  }

  double beatPhase = std::fmod(t * heartRateBpm / 60.0, 1.0);
  double pulse = std::sin(2.0 * TEST_PI * beatPhase);
  if (pulse < 0.0) {
    pulse = 0.0;
  }
  pulse = std::pow(pulse, 3.0) * pulseScale;
  double respiration = std::sin(2.0 * TEST_PI * respiratoryHz * t);

  int32_t ir = (int32_t)std::lround(100000.0 - (2600.0 * pulse) + (4200.0 * pulseScale * respiration));
  int32_t red = (int32_t)std::lround(90000.0 - (1200.0 * pulse) + (1300.0 * pulseScale * respiration));
  return RawSample{ir, red};
}

static SensorMetrics feedSyntheticWindows(MetricsProcessor& processor,
                                          int seconds,
                                          bool fingerOn = true,
                                          double pulseScale = 1.0,
                                          double heartRateBpm = 75.0,
                                          double respiratoryHz = 0.25) {
  SensorMetrics metrics{};
  RawSample window[PROCESS_BUFFER_SIZE];
  int totalSamples = seconds * SAMPLE_RATE;

  for (int start = 0; start < totalSamples; start += PROCESS_BUFFER_SIZE) {
    for (int i = 0; i < PROCESS_BUFFER_SIZE; i++) {
      double t = (start + i) / (double)SAMPLE_RATE;
      window[i] = makeSyntheticSample(t, heartRateBpm, respiratoryHz, fingerOn, pulseScale);
    }
    unsigned long windowEndMs = (unsigned long)((start + PROCESS_BUFFER_SIZE) * SAMPLE_PERIOD_MS);
    metrics = processor.processSamples(window, PROCESS_BUFFER_SIZE, windowEndMs);
  }

  return metrics;
}

void test_heart_rate_spo2_hrv_and_respiration_from_synthetic_ppg() {
  MetricsProcessor processor;
  SensorMetrics metrics = feedSyntheticWindows(processor, 40);

  TEST_ASSERT_TRUE(metrics.fingerDetected);
  TEST_ASSERT_GREATER_OR_EQUAL(2, metrics.confidence);
  TEST_ASSERT_TRUE(isValidMetric(metrics.bpm));
  TEST_ASSERT_FLOAT_WITHIN(8.0f, 75.0f, metrics.bpm);
  TEST_ASSERT_TRUE(isValidMetric(metrics.spo2));
  TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(70.0f, metrics.spo2);
  TEST_ASSERT_LESS_OR_EQUAL_FLOAT(100.0f, metrics.spo2);
  TEST_ASSERT_TRUE(isValidMetric(metrics.meanRR));
  TEST_ASSERT_FLOAT_WITHIN(180.0f, 800.0f, metrics.meanRR);
  TEST_ASSERT_TRUE(isValidMetric(metrics.sdnn));
  TEST_ASSERT_TRUE(isValidMetric(metrics.rmssd));
  TEST_ASSERT_TRUE(isValidMetric(metrics.pnn50));
  TEST_ASSERT_TRUE(isValidMetric(metrics.respiratoryRate));
  TEST_ASSERT_FLOAT_WITHIN(3.0f, 15.0f, metrics.respiratoryRate);
}

void test_hrv_and_respiratory_rate_are_invalid_before_enough_samples() {
  MetricsProcessor processor;
  SensorMetrics metrics = feedSyntheticWindows(processor, 5);

  TEST_ASSERT_TRUE(metrics.fingerDetected);
  TEST_ASSERT_TRUE(isValidMetric(metrics.bpm) || metrics.confidence <= 1);
  TEST_ASSERT_FALSE(isValidMetric(metrics.meanRR));
  TEST_ASSERT_FALSE(isValidMetric(metrics.sdnn));
  TEST_ASSERT_FALSE(isValidMetric(metrics.rmssd));
  TEST_ASSERT_FALSE(isValidMetric(metrics.pnn50));
  TEST_ASSERT_FALSE(isValidMetric(metrics.respiratoryRate));
}

void test_finger_off_invalidates_metrics() {
  MetricsProcessor processor;
  SensorMetrics metrics = feedSyntheticWindows(processor, 10, false);

  TEST_ASSERT_FALSE(metrics.fingerDetected);
  TEST_ASSERT_EQUAL(0, metrics.confidence);
  TEST_ASSERT_FALSE(isValidMetric(metrics.bpm));
  TEST_ASSERT_FALSE(isValidMetric(metrics.spo2));
  TEST_ASSERT_FALSE(isValidMetric(metrics.respiratoryRate));
  TEST_ASSERT_FALSE(isValidMetric(metrics.meanRR));
}

void test_weak_signal_quality_suppresses_hrv_and_respiration() {
  MetricsProcessor processor;
  SensorMetrics metrics = feedSyntheticWindows(processor, 20, true, 0.0);

  TEST_ASSERT_TRUE(metrics.fingerDetected);
  TEST_ASSERT_LESS_OR_EQUAL(1, metrics.confidence);
  TEST_ASSERT_FALSE(isValidMetric(metrics.meanRR));
  TEST_ASSERT_FALSE(isValidMetric(metrics.respiratoryRate));
}

void test_payload_formats_numbers_and_nulls() {
  SensorMetrics metrics;
  metrics.bpm = 78.4f;
  metrics.spo2 = 97.2f;
  metrics.respiratoryRate = NAN;
  metrics.meanRR = 769.2f;
  metrics.sdnn = 42.1f;
  metrics.rmssd = 35.8f;
  metrics.pnn50 = NAN;
  metrics.confidence = 3;
  metrics.fingerDetected = true;
  metrics.timestamp = 123456;

  std::string payload = buildMetricsPayload(metrics, DEVICE_ID, "1770000000000");

  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"deviceId\":\"WEARABLE-001\""));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"timestamp\":123456"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"epochMs\":1770000000000"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"fingerDetected\":true"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"confidence\":3"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"hr\":78.4"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"spo2\":97.2"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"respiratoryRate\":null"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"meanRR\":769.2"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"pnn50\":null"));
}

void test_payload_keeps_invalid_finger_off_snapshot_complete() {
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
  metrics.timestamp = 42000;

  std::string payload = buildMetricsPayload(metrics, DEVICE_ID, "null");

  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"fingerDetected\":false"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"confidence\":0"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"hr\":null"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"spo2\":null"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"hrv\":{\"meanRR\":null"));
}

void test_mock_publisher_receives_metrics_topic_once() {
  SensorMetrics metrics;
  metrics.bpm = 78.0f;
  metrics.spo2 = 98.0f;
  metrics.respiratoryRate = 15.0f;
  metrics.meanRR = 770.0f;
  metrics.sdnn = 20.0f;
  metrics.rmssd = 18.0f;
  metrics.pnn50 = 10.0f;
  metrics.confidence = 3;
  metrics.fingerDetected = true;
  metrics.timestamp = 1000;

  MockPublisher publisher;
  TEST_ASSERT_TRUE(publishMetrics(publisher, metrics, TOPIC_METRICS, DEVICE_ID, "null"));
  TEST_ASSERT_EQUAL(1, publisher.publishCount);
  TEST_ASSERT_EQUAL_STRING(TOPIC_METRICS, publisher.topic.c_str());
  TEST_ASSERT_NOT_EQUAL(std::string::npos, publisher.payload.find("\"hr\":78.0"));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_heart_rate_spo2_hrv_and_respiration_from_synthetic_ppg);
  RUN_TEST(test_hrv_and_respiratory_rate_are_invalid_before_enough_samples);
  RUN_TEST(test_finger_off_invalidates_metrics);
  RUN_TEST(test_weak_signal_quality_suppresses_hrv_and_respiration);
  RUN_TEST(test_payload_formats_numbers_and_nulls);
  RUN_TEST(test_payload_keeps_invalid_finger_off_snapshot_complete);
  RUN_TEST(test_mock_publisher_receives_metrics_topic_once);
  return UNITY_END();
}
