#pragma once

#include "apnea_model_data.h"

struct ApneaPrediction {
  int prediction;
  float apneaProbability;
};

float predictApneaRawScore(const float* features);
ApneaPrediction predictApnea(const float* features);
