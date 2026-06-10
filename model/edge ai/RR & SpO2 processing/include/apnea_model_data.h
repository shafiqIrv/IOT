#pragma once

#include <cstdint>

constexpr int APNEA_FEATURE_COUNT = 23;
constexpr int APNEA_TREE_COUNT = 300;
constexpr int APNEA_NODE_COUNT = 18300;

struct ApneaTreeNode {
  int16_t feature;
  int16_t left;
  int16_t right;
  bool defaultLeft;
  float threshold;
  float value;
};

extern const char* const APNEA_FEATURE_NAMES[APNEA_FEATURE_COUNT];
extern const float APNEA_IMPUTER_MEDIANS[APNEA_FEATURE_COUNT];
extern const ApneaTreeNode APNEA_MODEL_NODES[APNEA_NODE_COUNT];
extern const int16_t APNEA_TREE_ROOTS[APNEA_TREE_COUNT];
