from __future__ import annotations

import argparse
import math
from pathlib import Path

import joblib
import pandas as pd


SCRIPT_DIR = Path(__file__).resolve().parent
FIRMWARE_DIR = SCRIPT_DIR.parent
REPO_ROOT = FIRMWARE_DIR.parents[1]
MODEL_DIR = REPO_ROOT / "edge ai" / "prediction" / "apnea detection"
MODEL_PATH = MODEL_DIR / "models" / "apnea_detection_model.joblib"
FEATURE_COLUMNS_PATH = MODEL_DIR / "models" / "apnea_detection_feature_columns.joblib"
FEATURE_DATA_PATH = MODEL_DIR / "data" / "processed" / "apnea_features_1min.csv"
GENERATED_HEADER = FIRMWARE_DIR / "include" / "apnea_model_data.h"
GENERATED_SOURCE = FIRMWARE_DIR / "src" / "apnea_model_data.cpp"
GENERATED_TEST_VECTORS = FIRMWARE_DIR / "test" / "test_apnea_model" / "apnea_model_test_vectors.h"


def cpp_float(value: float) -> str:
  if math.isnan(value):
    return "NAN"
  if math.isinf(value):
    return "INFINITY" if value > 0 else "-INFINITY"
  return f"{float(value):.9e}f"


def cpp_string(value: str) -> str:
  return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def flatten_tree(tree_structure: dict, nodes: list[dict]) -> int:
  node_index = len(nodes)
  if "leaf_value" in tree_structure:
    nodes.append({
      "feature": -1,
      "threshold": 0.0,
      "left": -1,
      "right": -1,
      "default_left": True,
      "value": float(tree_structure["leaf_value"]),
    })
    return node_index

  if tree_structure.get("decision_type") != "<=":
    raise ValueError(f"Unsupported LightGBM decision type: {tree_structure.get('decision_type')}")

  nodes.append(None)
  left_index = flatten_tree(tree_structure["left_child"], nodes)
  right_index = flatten_tree(tree_structure["right_child"], nodes)
  nodes[node_index] = {
    "feature": int(tree_structure["split_feature"]),
    "threshold": float(tree_structure["threshold"]),
    "left": left_index,
    "right": right_index,
    "default_left": bool(tree_structure.get("default_left", True)),
    "value": 0.0,
  }
  return node_index


def generate_model_files() -> tuple[list[str], int, int]:
  pipeline = joblib.load(MODEL_PATH)
  feature_cols = list(joblib.load(FEATURE_COLUMNS_PATH))
  imputer = pipeline.named_steps["imputer"]
  estimator = pipeline.named_steps["model"]
  dump = estimator.booster_.dump_model()

  if dump.get("objective") != "binary sigmoid:1":
    raise ValueError(f"Unsupported LightGBM objective: {dump.get('objective')}")
  if dump.get("average_output"):
    raise ValueError("average_output=True is not supported by the firmware predictor")

  medians = [float(value) for value in imputer.statistics_]
  nodes: list[dict] = []
  tree_roots: list[int] = []
  for tree in dump["tree_info"]:
    tree_roots.append(flatten_tree(tree["tree_structure"], nodes))

  GENERATED_HEADER.write_text(
    "\n".join([
      "#pragma once",
      "",
      "#include <cstdint>",
      "",
      f"constexpr int APNEA_FEATURE_COUNT = {len(feature_cols)};",
      f"constexpr int APNEA_TREE_COUNT = {len(tree_roots)};",
      f"constexpr int APNEA_NODE_COUNT = {len(nodes)};",
      "",
      "struct ApneaTreeNode {",
      "  int16_t feature;",
      "  int16_t left;",
      "  int16_t right;",
      "  bool defaultLeft;",
      "  float threshold;",
      "  float value;",
      "};",
      "",
      "extern const char* const APNEA_FEATURE_NAMES[APNEA_FEATURE_COUNT];",
      "extern const float APNEA_IMPUTER_MEDIANS[APNEA_FEATURE_COUNT];",
      "extern const ApneaTreeNode APNEA_MODEL_NODES[APNEA_NODE_COUNT];",
      "extern const int16_t APNEA_TREE_ROOTS[APNEA_TREE_COUNT];",
      "",
    ]),
    encoding="utf-8",
  )

  feature_names = ",\n  ".join(cpp_string(name) for name in feature_cols)
  median_values = ",\n  ".join(cpp_float(value) for value in medians)
  tree_root_values = ",\n  ".join(str(root) for root in tree_roots)
  node_values = ",\n".join(
    (
      "  {"
      f"{node['feature']}, {node['left']}, {node['right']}, "
      f"{'true' if node['default_left'] else 'false'}, "
      f"{cpp_float(node['threshold'])}, {cpp_float(node['value'])}"
      "}"
    )
    for node in nodes
  )

  GENERATED_SOURCE.write_text(
    "\n".join([
      "#include \"apnea_model_data.h\"",
      "",
      "#include <cmath>",
      "",
      "const char* const APNEA_FEATURE_NAMES[APNEA_FEATURE_COUNT] = {",
      f"  {feature_names}",
      "};",
      "",
      "const float APNEA_IMPUTER_MEDIANS[APNEA_FEATURE_COUNT] = {",
      f"  {median_values}",
      "};",
      "",
      "const int16_t APNEA_TREE_ROOTS[APNEA_TREE_COUNT] = {",
      f"  {tree_root_values}",
      "};",
      "",
      "const ApneaTreeNode APNEA_MODEL_NODES[APNEA_NODE_COUNT] = {",
      node_values,
      "};",
      "",
    ]),
    encoding="utf-8",
  )

  return feature_cols, len(tree_roots), len(nodes)


def generate_test_vectors(feature_cols: list[str], limit: int = 6) -> None:
  pipeline = joblib.load(MODEL_PATH)
  df = pd.read_csv(FEATURE_DATA_PATH)
  valid_rows = df.dropna(subset=feature_cols).copy()
  selected = pd.concat([
    valid_rows[valid_rows["label"] == 0].head(limit // 2),
    valid_rows[valid_rows["label"] == 1].head(limit - (limit // 2)),
  ], ignore_index=True)

  if selected.empty:
    raise ValueError("No valid apnea feature rows were available for test vector generation")

  probabilities = pipeline.predict_proba(selected[feature_cols])[:, 1]
  predictions = pipeline.predict(selected[feature_cols])

  vector_blocks = []
  for row_index, (_, row) in enumerate(selected.iterrows()):
    features = ", ".join(cpp_float(float(row[col])) for col in feature_cols)
    vector_blocks.append(
      "  {"
      f"{{{features}}}, "
      f"{int(predictions[row_index])}, "
      f"{cpp_float(float(probabilities[row_index]))}"
      "}"
    )

  GENERATED_TEST_VECTORS.parent.mkdir(parents=True, exist_ok=True)
  GENERATED_TEST_VECTORS.write_text(
    "\n".join([
      "#pragma once",
      "",
      "#include \"apnea_model_data.h\"",
      "",
      "struct ApneaModelTestVector {",
      "  float features[APNEA_FEATURE_COUNT];",
      "  int prediction;",
      "  float probability;",
      "};",
      "",
      f"constexpr int APNEA_MODEL_TEST_VECTOR_COUNT = {len(vector_blocks)};",
      "const ApneaModelTestVector APNEA_MODEL_TEST_VECTORS[APNEA_MODEL_TEST_VECTOR_COUNT] = {",
      ",\n".join(vector_blocks),
      "};",
      "",
    ]),
    encoding="utf-8",
  )


def main() -> None:
  parser = argparse.ArgumentParser(description="Export the apnea LightGBM model to firmware C++ data.")
  parser.add_argument("--no-test-vectors", action="store_true", help="Skip generated native test vectors.")
  args = parser.parse_args()

  feature_cols, tree_count, node_count = generate_model_files()
  if not args.no_test_vectors:
    generate_test_vectors(feature_cols)

  print(f"Generated {GENERATED_HEADER}")
  print(f"Generated {GENERATED_SOURCE}")
  if not args.no_test_vectors:
    print(f"Generated {GENERATED_TEST_VECTORS}")
  print(f"Features: {len(feature_cols)} | Trees: {tree_count} | Nodes: {node_count}")


if __name__ == "__main__":
  main()
