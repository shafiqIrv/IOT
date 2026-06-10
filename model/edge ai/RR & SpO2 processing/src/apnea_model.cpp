#include "apnea_model.h"

#include <cmath>

static float imputedFeature(const float* features, int index) {
  float value = features[index];
  if (std::isnan(value) || std::isinf(value)) {
    return APNEA_IMPUTER_MEDIANS[index];
  }
  return value;
}

float predictApneaRawScore(const float* features) {
  float rawScore = 0.0f;

  for (int tree = 0; tree < APNEA_TREE_COUNT; tree++) {
    int nodeIndex = APNEA_TREE_ROOTS[tree];

    while (nodeIndex >= 0) {
      const ApneaTreeNode& node = APNEA_MODEL_NODES[nodeIndex];
      if (node.feature < 0) {
        rawScore += node.value;
        break;
      }

      float value = imputedFeature(features, node.feature);
      bool goLeft = (std::isnan(value) || std::isinf(value)) ? node.defaultLeft : value <= node.threshold;
      nodeIndex = goLeft ? node.left : node.right;
    }
  }

  return rawScore;
}

ApneaPrediction predictApnea(const float* features) {
  float rawScore = predictApneaRawScore(features);
  float probability;
  if (rawScore >= 0.0f) {
    float z = std::exp(-rawScore);
    probability = 1.0f / (1.0f + z);
  } else {
    float z = std::exp(rawScore);
    probability = z / (1.0f + z);
  }

  ApneaPrediction result;
  result.apneaProbability = probability;
  result.prediction = probability >= 0.5f ? 1 : 0;
  return result;
}
