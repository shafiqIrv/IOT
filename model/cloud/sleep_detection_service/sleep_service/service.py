from __future__ import annotations

import json
import logging
from typing import Any

import paho.mqtt.client as mqtt

from .config import Settings
from .features import SleepFeatureStore, invalid_sleep_output, parse_metrics_payload
from .model import SleepDetectionModel


LOGGER = logging.getLogger(__name__)


class SleepDetectionService:
  def __init__(self, settings: Settings, model: SleepDetectionModel | None = None) -> None:
    self.settings = settings
    self.model = model or SleepDetectionModel(settings.model_path, settings.feature_columns_path)
    self.features = SleepFeatureStore(
      window_sec=settings.window_sec,
      min_valid_samples=settings.min_valid_samples,
    )

  def output_topic(self, device_id: str) -> str:
    return self.settings.publish_topic_template.format(deviceId=device_id)

  def handle_payload(self, raw_payload: bytes | str) -> tuple[str, str] | None:
    try:
      sample = parse_metrics_payload(raw_payload)
    except Exception:
      LOGGER.exception("Failed to parse metrics payload")
      return None

    if not sample.valid_signal:
      output = invalid_sleep_output(sample, "invalid_signal", self.settings.window_sec)
      return self.output_topic(sample.device_id), dumps_json(output)

    self.features.add_sample(sample)
    feature_values, status = self.features.build_features(sample.device_id, sample.epoch_ms)
    if feature_values is None:
      output = invalid_sleep_output(sample, status, self.settings.window_sec)
      return self.output_topic(sample.device_id), dumps_json(output)

    output = self.model.predict(sample.device_id, sample.epoch_ms, feature_values, self.settings.window_sec)
    return self.output_topic(sample.device_id), dumps_json(output)


def dumps_json(payload: dict[str, Any]) -> str:
  return json.dumps(payload, separators=(",", ":"), allow_nan=False)


def run_service(settings: Settings) -> None:
  logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
  service = SleepDetectionService(settings)
  client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="sleep-detection-service")

  if settings.mqtt_username is not None:
    client.username_pw_set(settings.mqtt_username, settings.mqtt_password)

  def on_connect(client: mqtt.Client, userdata: object, flags: mqtt.ConnectFlags, reason_code: mqtt.ReasonCode, properties: mqtt.Properties | None) -> None:
    if reason_code == 0:
      LOGGER.info("Connected to MQTT broker %s:%s", settings.mqtt_host, settings.mqtt_port)
      client.subscribe(settings.subscribe_topic)
      LOGGER.info("Subscribed to %s", settings.subscribe_topic)
    else:
      LOGGER.error("MQTT connection failed: %s", reason_code)

  def on_message(client: mqtt.Client, userdata: object, message: mqtt.MQTTMessage) -> None:
    result = service.handle_payload(message.payload)
    if result is None:
      return

    topic, payload = result
    client.publish(topic, payload, qos=0, retain=False)
    LOGGER.info("Published sleep inference to %s", topic)

  client.on_connect = on_connect
  client.on_message = on_message
  client.connect(settings.mqtt_host, settings.mqtt_port, keepalive=60)
  client.loop_forever()
