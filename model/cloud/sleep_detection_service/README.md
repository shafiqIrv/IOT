# Sleep Detection Microservice

Python MQTT service that subscribes to ESP32 metrics, builds rolling sleep features, runs `sleep_detection_binary_model.joblib`, and publishes sleep inference.

## MQTT I/O

Input topic:

```text
sensor/+/metrics
```

Output topic template:

```text
sensor/{deviceId}/inference/sleep
```

Valid output example:

```json
{
  "deviceId": "WEARABLE-001",
  "epochMs": 1770000000000,
  "model": "sleep_detection",
  "windowSec": 30,
  "valid": true,
  "status": "ok",
  "isSleeping": 1,
  "label": "Sleeping",
  "sleepProbability": 0.82
}
```

Invalid or warming-up windows keep the same shape with `valid=false` and `isSleeping`, `label`, and `sleepProbability` as `null`.

## Run With Docker Compose

From the repository root:

```powershell
docker compose up --build
```

This starts Mosquitto and the sleep detection service.

## Run Tests

From the repository root:

```powershell
python -m pytest cloud\sleep_detection_service\tests
```

## Configuration

Copy `.env.example` if running outside Compose. Main variables:

```text
MQTT_HOST
MQTT_PORT
MQTT_SUBSCRIBE_TOPIC
MQTT_PUBLISH_TOPIC_TEMPLATE
SLEEP_MODEL_PATH
SLEEP_FEATURE_COLUMNS_PATH
SLEEP_WINDOW_SEC
SLEEP_MIN_VALID_SAMPLES
```
