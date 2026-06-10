from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

SERVICE_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(SERVICE_ROOT))

from sleep_service.config import Settings
from sleep_service.features import SLEEP_FEATURE_COLUMNS, SleepFeatureStore, parse_metrics_payload
from sleep_service.model import SleepDetectionModel
from sleep_service.service import SleepDetectionService


def make_payload(index: int, hr: float = 78.0, rr: float = 16.0, valid: bool = True) -> str:
  return json.dumps({
    "deviceId": "WEARABLE-001",
    "timestamp": index * 5000,
    "epochMs": 1770000000000 + (index * 5000),
    "fingerDetected": valid,
    "confidence": 3 if valid else 0,
    "hr": hr,
    "spo2": 97.0,
    "respiratoryRate": rr,
    "hrv": {
      "meanRR": 769.2,
      "sdnn": 42.1,
      "rmssd": 35.8,
      "pnn50": 18.5,
    },
  })


def test_parse_metrics_payload_accepts_existing_esp32_shape() -> None:
  sample = parse_metrics_payload(make_payload(0))

  assert sample.device_id == "WEARABLE-001"
  assert sample.epoch_ms == 1770000000000
  assert sample.valid_signal
  assert sample.heart_rate == 78.0
  assert sample.respiratory_rate == 16.0


def test_sleep_feature_store_builds_expected_feature_columns() -> None:
  store = SleepFeatureStore(window_sec=30, min_valid_samples=3)
  for index in range(3):
    store.add_sample(parse_metrics_payload(make_payload(index, hr=76.0 + index, rr=15.0 + index)))

  features, status = store.build_features("WEARABLE-001")

  assert status == "ok"
  assert features is not None
  assert list(features.keys()) == SLEEP_FEATURE_COLUMNS
  assert features["heart_rate"] == 78.0
  assert features["respiratory_rate"] == 17.0
  assert features["hr_mean"] == pytest.approx(77.0)
  assert features["rr_mean"] == pytest.approx(16.0)
  assert features["hr_rr_ratio"] == pytest.approx(78.0 / 17.0)


class DummySleepModel:
  def predict(self, device_id: str, epoch_ms: int, features: dict[str, float], window_sec: int) -> dict:
    assert list(features.keys()) == SLEEP_FEATURE_COLUMNS
    return {
      "deviceId": device_id,
      "epochMs": epoch_ms,
      "model": "sleep_detection",
      "windowSec": window_sec,
      "valid": True,
      "status": "ok",
      "isSleeping": 1,
      "label": "Sleeping",
      "sleepProbability": 0.75,
    }


def test_service_outputs_invalid_signal_and_valid_inference() -> None:
  settings = Settings(
    mqtt_host="localhost",
    mqtt_port=1883,
    mqtt_username=None,
    mqtt_password=None,
    subscribe_topic="sensor/+/metrics",
    publish_topic_template="sensor/{deviceId}/inference/sleep",
    model_path=Path("unused.joblib"),
    feature_columns_path=Path("unused_features.joblib"),
    window_sec=30,
    min_valid_samples=3,
  )
  service = SleepDetectionService(settings, model=DummySleepModel())

  invalid_result = service.handle_payload(make_payload(0, valid=False))
  assert invalid_result is not None
  invalid_topic, invalid_payload = invalid_result
  assert invalid_topic == "sensor/WEARABLE-001/inference/sleep"
  invalid_json = json.loads(invalid_payload)
  assert invalid_json["valid"] is False
  assert invalid_json["status"] == "invalid_signal"

  for index in range(3):
    result = service.handle_payload(make_payload(index, hr=76.0 + index, rr=15.0 + index))

  assert result is not None
  topic, payload = result
  assert topic == "sensor/WEARABLE-001/inference/sleep"
  output = json.loads(payload)
  assert output["valid"] is True
  assert output["isSleeping"] == 1
  assert output["sleepProbability"] == 0.75


def test_saved_sleep_model_smoke_prediction() -> None:
  pytest.importorskip("xgboost")
  model_path = REPO_ROOT / "edge ai" / "prediction" / "sleep detection" / "sleep_detection_binary_model.joblib"
  feature_columns_path = REPO_ROOT / "edge ai" / "prediction" / "sleep detection" / "sleep_detection_feature_columns.joblib"
  model = SleepDetectionModel(model_path, feature_columns_path)

  features = {
    "heart_rate": 78.0,
    "respiratory_rate": 16.2,
    "hr_mean": 76.5,
    "hr_sdnn_5": 2.1,
    "hr_rmssd_5": 2.7,
    "hr_slope_3": -0.4,
    "rr_mean": 15.8,
    "rr_sd_5": 2.1,
    "rr_slope_3": -0.2,
    "hr_rr_ratio": 4.8,
    "hr_rr_product": 1236.0,
  }
  output = model.predict("WEARABLE-001", 1770000000000, features, 30)

  assert output["valid"] is True
  assert output["label"] in {"Sleeping", "Not Sleeping"}
  assert output["isSleeping"] in {0, 1}
  assert 0.0 <= output["sleepProbability"] <= 1.0
