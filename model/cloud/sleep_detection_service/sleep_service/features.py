from __future__ import annotations

import json
import math
import time
from collections import defaultdict, deque
from dataclasses import dataclass
from typing import Any

import numpy as np


SLEEP_FEATURE_COLUMNS = [
  "heart_rate",
  "respiratory_rate",
  "hr_mean",
  "hr_sdnn_5",
  "hr_rmssd_5",
  "hr_slope_3",
  "rr_mean",
  "rr_sd_5",
  "rr_slope_3",
  "hr_rr_ratio",
  "hr_rr_product",
]


@dataclass(frozen=True)
class MetricSample:
  device_id: str
  epoch_ms: int
  heart_rate: float
  respiratory_rate: float
  confidence: int
  finger_detected: bool

  @property
  def valid_signal(self) -> bool:
    return (
      self.finger_detected
      and self.confidence >= 2
      and is_finite(self.heart_rate)
      and is_finite(self.respiratory_rate)
      and self.heart_rate > 0.0
      and self.respiratory_rate > 0.0
    )


def is_finite(value: Any) -> bool:
  try:
    return math.isfinite(float(value))
  except (TypeError, ValueError):
    return False


def _required_float(payload: dict[str, Any], key: str) -> float:
  value = payload.get(key)
  if not is_finite(value):
    return math.nan
  return float(value)


def parse_metrics_payload(raw_payload: bytes | str, now_ms: int | None = None) -> MetricSample:
  if isinstance(raw_payload, bytes):
    raw_payload = raw_payload.decode("utf-8")

  payload = json.loads(raw_payload)
  device_id = str(payload.get("deviceId") or "")
  if not device_id:
    raise ValueError("payload is missing deviceId")

  epoch_ms_value = payload.get("epochMs")
  if isinstance(epoch_ms_value, int):
    epoch_ms = epoch_ms_value
  elif is_finite(epoch_ms_value):
    epoch_ms = int(float(epoch_ms_value))
  else:
    epoch_ms = now_ms if now_ms is not None else int(time.time() * 1000)

  return MetricSample(
    device_id=device_id,
    epoch_ms=epoch_ms,
    heart_rate=_required_float(payload, "hr"),
    respiratory_rate=_required_float(payload, "respiratoryRate"),
    confidence=int(payload.get("confidence") or 0),
    finger_detected=bool(payload.get("fingerDetected")),
  )


def _mean(values: list[float]) -> float:
  if not values:
    return math.nan
  return float(np.mean(values))


def _sample_std(values: list[float]) -> float:
  if len(values) < 2:
    return 0.0
  return float(np.std(values, ddof=1))


def _rmssd(values: list[float]) -> float:
  if len(values) < 2:
    return 0.0
  diffs = np.diff(np.asarray(values, dtype=float))
  return float(np.sqrt(np.mean(diffs * diffs)))


def _slope(values: list[float]) -> float:
  if len(values) < 2:
    return 0.0
  x = np.arange(len(values), dtype=float)
  y = np.asarray(values, dtype=float)
  x_mean = float(np.mean(x))
  y_mean = float(np.mean(y))
  denominator = float(np.sum((x - x_mean) ** 2))
  if denominator <= 0.0:
    return 0.0
  return float(np.sum((x - x_mean) * (y - y_mean)) / denominator)


class SleepFeatureStore:
  def __init__(self, window_sec: int = 30, min_valid_samples: int = 3) -> None:
    self.window_ms = window_sec * 1000
    self.min_valid_samples = min_valid_samples
    self._samples: dict[str, deque[MetricSample]] = defaultdict(deque)

  def add_sample(self, sample: MetricSample) -> None:
    if not sample.valid_signal:
      return

    device_samples = self._samples[sample.device_id]
    device_samples.append(sample)
    self._prune(sample.device_id, sample.epoch_ms)

  def _prune(self, device_id: str, now_ms: int) -> None:
    device_samples = self._samples[device_id]
    while device_samples and now_ms - device_samples[0].epoch_ms > self.window_ms:
      device_samples.popleft()

  def build_features(self, device_id: str, now_ms: int | None = None) -> tuple[dict[str, float] | None, str]:
    device_samples = self._samples.get(device_id)
    if not device_samples:
      return None, "warming_up"

    if now_ms is not None:
      self._prune(device_id, now_ms)
      device_samples = self._samples.get(device_id)

    if device_samples is None or len(device_samples) < self.min_valid_samples:
      return None, "warming_up"

    recent = list(device_samples)
    last = recent[-1]
    heart_rates = [sample.heart_rate for sample in recent[-5:]]
    respiratory_rates = [sample.respiratory_rate for sample in recent[-5:]]
    heart_rate_slope_values = [sample.heart_rate for sample in recent[-3:]]
    respiratory_slope_values = [sample.respiratory_rate for sample in recent[-3:]]

    features = {
      "heart_rate": last.heart_rate,
      "respiratory_rate": last.respiratory_rate,
      "hr_mean": _mean(heart_rates),
      "hr_sdnn_5": _sample_std(heart_rates),
      "hr_rmssd_5": _rmssd(heart_rates),
      "hr_slope_3": _slope(heart_rate_slope_values),
      "rr_mean": _mean(respiratory_rates),
      "rr_sd_5": _sample_std(respiratory_rates),
      "rr_slope_3": _slope(respiratory_slope_values),
      "hr_rr_ratio": last.heart_rate / last.respiratory_rate,
      "hr_rr_product": last.heart_rate * last.respiratory_rate,
    }
    return features, "ok"


def invalid_sleep_output(sample: MetricSample, status: str, window_sec: int) -> dict[str, Any]:
  return {
    "deviceId": sample.device_id,
    "epochMs": sample.epoch_ms,
    "model": "sleep_detection",
    "windowSec": window_sec,
    "valid": False,
    "status": status,
    "isSleeping": None,
    "label": None,
    "sleepProbability": None,
  }
