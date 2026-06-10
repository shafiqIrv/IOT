from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Settings:
  mqtt_host: str
  mqtt_port: int
  mqtt_username: str | None
  mqtt_password: str | None
  subscribe_topic: str
  publish_topic_template: str
  model_path: Path
  feature_columns_path: Path
  window_sec: int
  min_valid_samples: int


def _optional_env(name: str) -> str | None:
  value = os.getenv(name)
  if value is None or value == "":
    return None
  return value


def load_settings() -> Settings:
  return Settings(
    mqtt_host=os.getenv("MQTT_HOST", "localhost"),
    mqtt_port=int(os.getenv("MQTT_PORT", "1883")),
    mqtt_username=_optional_env("MQTT_USERNAME"),
    mqtt_password=_optional_env("MQTT_PASSWORD"),
    subscribe_topic=os.getenv("MQTT_SUBSCRIBE_TOPIC", "sensor/+/live"),
    publish_topic_template=os.getenv("MQTT_PUBLISH_TOPIC_TEMPLATE", "sensor/{deviceId}/inference/sleep"),
    model_path=Path(os.getenv("SLEEP_MODEL_PATH", "/models/sleep_detection_binary_model.joblib")),
    feature_columns_path=Path(os.getenv("SLEEP_FEATURE_COLUMNS_PATH", "/models/sleep_detection_feature_columns.joblib")),
    window_sec=int(os.getenv("SLEEP_WINDOW_SEC", "30")),
    min_valid_samples=int(os.getenv("SLEEP_MIN_VALID_SAMPLES", "3")),
  )
