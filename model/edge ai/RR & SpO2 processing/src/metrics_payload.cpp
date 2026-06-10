#include "metrics_payload.h"

#include <cmath>
#include <cstdio>

#include "metrics_processor.h"

std::string jsonNumberOrNull(float value, int decimals) {
  if (!isValidMetric(value)) {
    return "null";
  }

  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  return std::string(buffer);
}

std::string buildMetricsPayload(const SensorMetrics& metrics,
                                const char* deviceId,
                                const std::string& epochMsJson) {
  std::string payload;
  payload.reserve(512);
  payload += "{";
  payload += "\"deviceId\":\"";
  payload += deviceId;
  payload += "\",\"timestamp\":";
  payload += std::to_string(metrics.timestamp);
  payload += ",\"epochMs\":";
  payload += epochMsJson.empty() ? "null" : epochMsJson;
  payload += ",\"fingerDetected\":";
  payload += metrics.fingerDetected ? "true" : "false";
  payload += ",\"confidence\":";
  payload += std::to_string(metrics.confidence);
  payload += ",\"hr\":";
  payload += jsonNumberOrNull(metrics.bpm, 1);
  payload += ",\"spo2\":";
  payload += jsonNumberOrNull(metrics.spo2, 1);
  payload += ",\"respiratoryRate\":";
  payload += jsonNumberOrNull(metrics.respiratoryRate, 1);
  payload += ",\"hrv\":{";
  payload += "\"meanRR\":";
  payload += jsonNumberOrNull(metrics.meanRR, 1);
  payload += ",\"sdnn\":";
  payload += jsonNumberOrNull(metrics.sdnn, 1);
  payload += ",\"rmssd\":";
  payload += jsonNumberOrNull(metrics.rmssd, 1);
  payload += ",\"pnn50\":";
  payload += jsonNumberOrNull(metrics.pnn50, 1);
  payload += "}}";
  return payload;
}

std::string buildApneaPayload(const ApneaInference& inference,
                              const char* deviceId,
                              const std::string& epochMsJson) {
  std::string payload;
  payload.reserve(384);
  payload += "{";
  payload += "\"deviceId\":\"";
  payload += deviceId;
  payload += "\",\"timestamp\":";
  payload += std::to_string(inference.timestamp);
  payload += ",\"epochMs\":";
  payload += epochMsJson.empty() ? "null" : epochMsJson;
  payload += ",\"model\":\"apnea_detection\"";
  payload += ",\"windowSec\":";
  payload += std::to_string(inference.windowSec);
  payload += ",\"valid\":";
  payload += inference.valid ? "true" : "false";
  payload += ",\"status\":\"";
  payload += inference.status != nullptr ? inference.status : "unknown";
  payload += "\",\"prediction\":";
  payload += inference.valid ? std::to_string(inference.prediction) : "null";
  payload += ",\"label\":";
  if (inference.valid && inference.label != nullptr) {
    payload += "\"";
    payload += inference.label;
    payload += "\"";
  } else {
    payload += "null";
  }
  payload += ",\"apneaProbability\":";
  payload += jsonNumberOrNull(inference.apneaProbability, 4);
  payload += ",\"confidence\":";
  payload += std::to_string(inference.confidence);
  payload += "}";
  return payload;
}

bool MockPublisher::publish(const char* targetTopic, const std::string& targetPayload) {
  topic = targetTopic;
  payload = targetPayload;
  publishCount++;
  return true;
}

bool publishMetrics(MockPublisher& publisher,
                    const SensorMetrics& metrics,
                    const char* topic,
                    const char* deviceId,
                    const std::string& epochMsJson) {
  return publisher.publish(topic, buildMetricsPayload(metrics, deviceId, epochMsJson));
}

bool publishApneaInference(MockPublisher& publisher,
                           const ApneaInference& inference,
                           const char* topic,
                           const char* deviceId,
                           const std::string& epochMsJson) {
  return publisher.publish(topic, buildApneaPayload(inference, deviceId, epochMsJson));
}
