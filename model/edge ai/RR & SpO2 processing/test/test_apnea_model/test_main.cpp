#include <unity.h>

#include <cmath>
#include <cstring>
#include <string>

#include "apnea_features.h"
#include "apnea_model.h"
#include "apnea_model_test_vectors.h"
#include "metrics_payload.h"

void setUp() {}

void tearDown() {}

static SensorMetrics makeMetrics(unsigned long timestamp, float bpm, float spo2, int confidence = 3) {
  SensorMetrics metrics;
  metrics.bpm = bpm;
  metrics.spo2 = spo2;
  metrics.respiratoryRate = 16.0f;
  metrics.meanRR = 60000.0f / bpm;
  metrics.sdnn = 32.0f;
  metrics.rmssd = 28.0f;
  metrics.pnn50 = 12.0f;
  metrics.confidence = confidence;
  metrics.fingerDetected = confidence >= 2;
  metrics.timestamp = timestamp;
  return metrics;
}

void test_apnea_model_matches_generated_python_vectors() {
  for (int i = 0; i < APNEA_MODEL_TEST_VECTOR_COUNT; i++) {
    ApneaPrediction prediction = predictApnea(APNEA_MODEL_TEST_VECTORS[i].features);
    TEST_ASSERT_EQUAL(APNEA_MODEL_TEST_VECTORS[i].prediction, prediction.prediction);
    TEST_ASSERT_FLOAT_WITHIN(0.0002f, APNEA_MODEL_TEST_VECTORS[i].probability, prediction.apneaProbability);
  }
}

void test_apnea_feature_accumulator_builds_valid_window() {
  ApneaFeatureAccumulator accumulator;
  unsigned long timestamp = 5000;

  for (int window = 0; window < 12; window++) {
    float rrIntervals[5] = {
      790.0f + window,
      805.0f - window,
      815.0f,
      780.0f + (window % 3),
      800.0f,
    };
    float spo2 = window == 7 ? 92.0f : 96.0f - (window % 2);
    accumulator.addMetrics(makeMetrics(timestamp, 75.0f, spo2), rrIntervals, 5);
    timestamp += 5000;
  }

  ApneaFeatures features = accumulator.buildFeatures(60000);
  TEST_ASSERT_TRUE(features.valid);
  TEST_ASSERT_EQUAL_STRING("ok", features.status);
  TEST_ASSERT_EQUAL(60, features.rrCount);
  TEST_ASSERT_EQUAL(12, features.spo2Count);
  TEST_ASSERT_TRUE(features.values[0] > 760.0f);
  TEST_ASSERT_TRUE(features.values[4] > 70.0f);
  TEST_ASSERT_TRUE(features.values[12] > 90.0f);
  TEST_ASSERT_TRUE(features.values[17] >= 3.0f);
}

void test_apnea_feature_accumulator_reports_warming_up() {
  ApneaFeatureAccumulator accumulator;
  float rrIntervals[3] = {800.0f, 810.0f, 790.0f};
  accumulator.addMetrics(makeMetrics(5000, 75.0f, 96.0f), rrIntervals, 3);

  ApneaFeatures features = accumulator.buildFeatures(5000);
  TEST_ASSERT_FALSE(features.valid);
  TEST_ASSERT_EQUAL_STRING("warming_up", features.status);
}

void test_apnea_payload_formats_valid_and_invalid_results() {
  ApneaInference valid;
  valid.valid = true;
  valid.status = "ok";
  valid.prediction = 1;
  valid.label = "apnea";
  valid.apneaProbability = 0.87234f;
  valid.confidence = 3;
  valid.timestamp = 123456;
  valid.windowSec = 60;

  std::string payload = buildApneaPayload(valid, DEVICE_ID, "1770000000000");
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"model\":\"apnea_detection\""));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"valid\":true"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"prediction\":1"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"label\":\"apnea\""));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"apneaProbability\":0.8723"));

  ApneaInference invalid = valid;
  invalid.valid = false;
  invalid.status = "warming_up";
  invalid.prediction = -1;
  invalid.label = nullptr;
  invalid.apneaProbability = NAN;

  payload = buildApneaPayload(invalid, DEVICE_ID, "null");
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"valid\":false"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"status\":\"warming_up\""));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"prediction\":null"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"label\":null"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"apneaProbability\":null"));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_apnea_model_matches_generated_python_vectors);
  RUN_TEST(test_apnea_feature_accumulator_builds_valid_window);
  RUN_TEST(test_apnea_feature_accumulator_reports_warming_up);
  RUN_TEST(test_apnea_payload_formats_valid_and_invalid_results);
  return UNITY_END();
}
