#pragma once

#include <string>
#include "apnea_features.h"
#include "metrics_types.h"

std::string jsonNumberOrNull(float value, int decimals);
std::string buildMetricsPayload(const SensorMetrics& metrics,
                                const char* deviceId,
                                const std::string& epochMsJson);
std::string buildApneaPayload(const ApneaInference& inference,
                              const char* deviceId,
                              const std::string& epochMsJson);

struct MockPublisher {
  std::string topic;
  std::string payload;
  int publishCount = 0;

  bool publish(const char* targetTopic, const std::string& targetPayload);
};

bool publishMetrics(MockPublisher& publisher,
                    const SensorMetrics& metrics,
                    const char* topic,
                    const char* deviceId,
                    const std::string& epochMsJson);
bool publishApneaInference(MockPublisher& publisher,
                           const ApneaInference& inference,
                           const char* topic,
                           const char* deviceId,
                           const std::string& epochMsJson);
