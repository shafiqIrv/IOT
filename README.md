# IoT Sleep & Apnea Monitoring System

End-to-end system that tracks sleep and detects sleep apnea from a wearable pulse-oximeter.

- **Apnea detection** runs **on the edge** (ESP32-S3 + MAX30102), on-device inference.
- **Sleep detection** runs in the **cloud** (a Python/XGBoost service, hosted locally via Docker for now).
- A **dashboard** (Next.js + Node.js + PostgreSQL) displays apnea results, a sleep/awake graph, and live HR / SpO₂ with an optional HRV view.

Everything talks over a single **MQTT broker (Mosquitto)**.

---

## Architecture

```
┌─────────────────────────┐
│  ESP32-S3 + MAX30102    │   Edge device (firmware via PlatformIO)
│  • HR, SpO2, Resp, HRV  │
│  • Apnea inference      │
└───────────┬─────────────┘
            │ MQTT (Wi-Fi → host LAN IP:1883)
            │  publishes:
            │   sensor/wearable-001/metrics
            │   sensor/wearable-001/inference/apnea
            ▼
┌─────────────────────────────────────────────────────────────┐
│                    Docker Compose stack                      │
│                                                              │
│   ┌──────────┐   metrics    ┌───────────────────────────┐   │
│   │ Mosquitto│◀────────────▶│ sleep_detection_service   │   │
│   │  (mqtt)  │   sleep inf. │ (Python / XGBoost)        │   │
│   │  :1883   │◀─────────────│ publishes:                │   │
│   └────┬─────┘              │ sensor/{id}/inference/sleep│  │
│        │ all sensor/* topics└───────────────────────────┘   │
│        ▼                                                     │
│   ┌──────────┐   INSERT     ┌──────────┐   /api/data        │
│   │ backend  │─────────────▶│ Postgres │◀───────┐           │
│   │ (Node.js)│              │  (db)    │        │            │
│   │  :4000   │              │  :5432   │   ┌────┴─────┐      │
│   └──────────┘              └──────────┘   │ frontend │      │
│                                            │ (Next.js)│      │
│                                            │  :3000   │      │
│                                            └──────────┘      │
└─────────────────────────────────────────────────────────────┘
```

### MQTT topics & payloads

| Topic | Producer | Payload (key fields) |
|-------|----------|----------------------|
| `sensor/wearable-001/metrics` | ESP32 (~every 5 s) | `deviceId, epochMs, fingerDetected, confidence, hr, spo2, respiratoryRate, hrv:{meanRR,sdnn,rmssd,pnn50}` |
| `sensor/wearable-001/inference/apnea` | ESP32 (~every 60 s) | `deviceId, epochMs, model:"apnea_detection", valid, prediction, label, apneaProbability, confidence, windowSec` |
| `sensor/wearable-001/inference/sleep` | cloud service | `deviceId, epochMs, model:"sleep_detection", valid, isSleeping, label, sleepProbability, windowSec` |

The cloud service subscribes to `sensor/+/metrics`, buffers a 30 s window, and publishes a sleep inference. The backend subscribes to all three topics and writes to PostgreSQL; the frontend polls `/api/data` every 3 s.

---

## Repository layout

```
IOT/
├── docker-compose.yaml        # Unified stack — run this
├── dashboard/                 # Frontend + backend + db + mosquitto config
│   ├── backend/               # Node.js MQTT → PostgreSQL ingester
│   ├── frontend/              # Next.js dashboard (cards + charts)
│   ├── db-init/init.sql       # PostgreSQL schema (auto-applied)
│   └── mosquitto/             # Broker config (shared by the whole stack)
├── model/
│   ├── cloud/sleep_detection_service/   # Python/XGBoost sleep model service
│   └── edge ai/RR & SpO2 processing/    # ESP32-S3 firmware (PlatformIO)
└── ML/                        # Research / training notebooks (not part of runtime)
```

---

## Prerequisites

- **Docker Desktop** (Docker + Docker Compose v2).
- **PlatformIO** (VS Code extension or `pip install platformio`) — to build/flash the ESP32.
- An **ESP32-S3 DevKitC-1** wired to a **MAX30102** sensor (I²C), plus a USB cable.
- The PC running Docker and the ESP32 must be on the **same Wi-Fi/LAN**.

---

## 1. Start the cloud + dashboard stack

From the repository root (`IOT/`):

```bash
# First run, or whenever the DB schema changes, reset the volume:
docker compose down -v

docker compose up --build
```

This starts five services:

| Service | Port | Description |
|---------|------|-------------|
| `mqtt` | 1883 | Mosquitto broker (anonymous access; reachable on your LAN) |
| `db` | 5432 | PostgreSQL (`admin` / `adminpassword`, db `health_db`) |
| `backend` | 4000 | MQTT → PostgreSQL ingester |
| `frontend` | 3000 | Dashboard UI |
| `sleep_detection_service` | — | Cloud sleep model (MQTT in/out) |

Open the dashboard at **http://localhost:3000**.

> **Note:** until the ESP32 (or a test publisher) sends data, the dashboard shows
> *"Data Belum Tersedia"*.

### Find your host's LAN IP (needed for the firmware)

- **Windows:** `ipconfig` → IPv4 Address (e.g. `192.168.1.23`)
- **macOS/Linux:** `ipconfig getifaddr en0` / `hostname -I`

---

## 2. Flash the ESP32 edge device

Edit [model/edge ai/RR & SpO2 processing/src/main.cpp](model/edge%20ai/RR%20&%20SpO2%20processing/src/main.cpp):

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MQTT_BROKER   = "192.168.1.23";   // ← your host's LAN IP from step 1
const int   MQTT_PORT     = 1883;
```

Then build & upload (from the firmware folder):

```bash
cd "model/edge ai/RR & SpO2 processing"
pio run -e esp32dev --target upload
pio device monitor          # watch serial @ 115200 baud
```

On success the serial log prints `[MQTT] Metrics terkirim` (and apnea inferences each minute). Place a finger on the MAX30102 so `fingerDetected` is true and HR/SpO₂ are valid.

---

## 3. View results on the dashboard

At **http://localhost:3000** you'll see:

- **Heart Rate, SpO₂, Respiratory Rate** — live cards.
- **Status Vital** — `Kritis` / `Normal`, derived from thresholds (SpO₂ < 92, HR < 50 or > 120).
- **Status Tidur** — latest sleep state + probability (from the cloud model).
- **Deteksi Apnea** — `Terdeteksi` / `Aman` + probability (from the edge model).
- **HRV toggle** — switch on to reveal SDNN / RMSSD / pNN50 cards and add an HRV line to the trend chart.
- **Riwayat Heart Rate & SpO₂** — time-series trend chart.
- **Riwayat Status Tidur** — sleep/awake step graph over time.

---

## Testing without hardware

You can drive the whole pipeline with `mosquitto_pub` (install `mosquitto-clients`, or use any MQTT client).

```bash
# Send several metrics frames a few seconds apart — after ~3 frames within 30s,
# the cloud service will emit a sleep inference.
mosquitto_pub -h localhost -t sensor/wearable-001/metrics -m \
'{"deviceId":"WEARABLE-001","epochMs":1771000000000,"fingerDetected":true,"confidence":3,"hr":68,"spo2":97,"respiratoryRate":14,"hrv":{"meanRR":880,"sdnn":42,"rmssd":35,"pnn50":20}}'

# Send an apnea inference to light up the apnea card.
mosquitto_pub -h localhost -t sensor/wearable-001/inference/apnea -m \
'{"deviceId":"WEARABLE-001","epochMs":1771000000000,"model":"apnea_detection","valid":true,"status":"ok","prediction":1,"label":"apnea","apneaProbability":0.87,"confidence":3,"windowSec":60}'
```

Watch the `backend` logs for `[VITALS]`, `[SLEEP]`, `[APNEA]` confirmations:

```bash
docker compose logs -f backend sleep_detection_service
```

---

## Common tasks

| Task | Command |
|------|---------|
| Start the stack | `docker compose up --build` |
| Stop | `docker compose down` |
| Reset database (apply schema changes) | `docker compose down -v` then up |
| Tail logs | `docker compose logs -f backend frontend sleep_detection_service` |
| Inspect DB | `docker compose exec db psql -U admin -d health_db` |
| Run sleep-service unit tests | `cd model/cloud/sleep_detection_service && pytest` |

---

## Troubleshooting

- **Dashboard stuck on "Data Belum Tersedia"** — no data yet. Confirm the ESP32 (or a test `mosquitto_pub`) is publishing; check `docker compose logs -f backend`.
- **ESP32 can't connect to MQTT** — `MQTT_BROKER` must be the host's **LAN IP** (not `localhost`/`127.0.0.1`), device and PC on the same network, and a firewall allowing inbound **TCP 1883**.
- **Sleep status stays "Menunggu data..."** — the cloud model needs ≥ 3 valid metrics frames within its 30 s window, with `fingerDetected:true` and `confidence ≥ 2`.
- **Schema changes not taking effect** — Postgres only runs `init.sql` on an empty data volume; run `docker compose down -v` to recreate it.
- **Apnea card never triggers** — apnea inference is published only every ~60 s and only when `valid:true`.
```
