from __future__ import annotations

from pathlib import Path
from typing import Any

import joblib
import pandas as pd


class SleepDetectionModel:
  def __init__(self, model_path: Path, feature_columns_path: Path) -> None:
    self.model = joblib.load(model_path)
    self.feature_columns = list(joblib.load(feature_columns_path))

  def predict(self, device_id: str, epoch_ms: int, features: dict[str, float], window_sec: int) -> dict[str, Any]:
    missing = [column for column in self.feature_columns if column not in features]
    if missing:
      raise ValueError(f"Missing sleep feature columns: {missing}")

    sample_df = pd.DataFrame([features])[self.feature_columns]
    prediction = int(self.model.predict(sample_df)[0])
    probability = None

    if hasattr(self.model, "predict_proba"):
      proba = self.model.predict_proba(sample_df)[0]
      classes = list(getattr(self.model, "classes_", []))
      if 1 in classes:
        probability = float(proba[classes.index(1)])
      elif len(proba) > 1:
        probability = float(proba[1])

    return {
      "deviceId": device_id,
      "epochMs": epoch_ms,
      "model": "sleep_detection",
      "windowSec": window_sec,
      "valid": True,
      "status": "ok",
      "isSleeping": prediction,
      "label": "Sleeping" if prediction == 1 else "Not Sleeping",
      "sleepProbability": probability,
    }
