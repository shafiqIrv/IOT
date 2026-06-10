# MAX30102 Metrics Pipeline untuk ESP32-S3

Project ini membaca sinyal PPG dari MAX30102/MAX30105-compatible sensor di ESP32-S3, lalu menghitung:

- Heart Rate / BPM
- SpO2
- HRV/PRV metrics: `meanRR`, `sdnn`, `rmssd`, `pnn50`
- Respiratory Rate estimate dari modulasi PPG
- Quality/confidence signal
- Payload JSON MQTT dalam satu topic

> Catatan: respiratory rate dari MAX30102 bukan pengukuran langsung. Nilai ini adalah estimasi dari modulasi sinyal PPG, sehingga perlu validasi sensor fisik dan kualitas sinyal.

## Struktur Project

```text
.
├── platformio.ini
├── include/
│   ├── metrics_types.h
│   ├── metrics_processor.h
│   └── metrics_payload.h
├── src/
│   ├── main.cpp
│   ├── metrics_processor.cpp
│   └── metrics_payload.cpp
└── test/
    └── test_metrics_pipeline/
        └── test_main.cpp
```

### `platformio.ini`

Berisi dua environment:

- `esp32-s3-devkitc-1`: build firmware untuk ESP32-S3.
- `native`: unit test di PC tanpa hardware.

Dependency firmware:

- SparkFun MAX3010x Pulse and Proximity Sensor Library
- PubSubClient
- WiFi dan Wire dari Arduino ESP32 framework

### `src/main.cpp`

File ini khusus untuk hardware dan runtime ESP32:

- Inisialisasi WiFi
- Inisialisasi MQTT
- Inisialisasi MAX30102
- FreeRTOS dual-core task
- Double buffering raw sample
- Publish payload ke MQTT

`main.cpp` tidak lagi berisi algoritma utama processing. Sensor task hanya membaca raw IR/Red dari FIFO sensor, lalu process task mengirim buffer ke `MetricsProcessor`.

### `include/metrics_types.h`

Berisi konstanta dan tipe data bersama:

- `SAMPLE_RATE = 100`
- `PROCESS_BUFFER_SIZE = 500`
- `RR_BUFFER_SIZE = 64`
- `RESP_DS_SIZE = 300`
- `DEVICE_ID = "WEARABLE-001"`
- `TOPIC_METRICS = "sensor/wearable-001/metrics"`
- `SensorMetrics`
- `RawSample`
- `RRIntervalBuffer`

Output utama processing adalah:

```cpp
struct SensorMetrics {
  float bpm;
  float spo2;
  float respiratoryRate;
  float meanRR;
  float sdnn;
  float rmssd;
  float pnn50;
  int confidence;
  bool fingerDetected;
  unsigned long timestamp;
};
```

### `src/metrics_processor.cpp`

Berisi logic portable yang bisa dites tanpa Arduino:

- Filtering IR
- Peak detection
- BPM dari RR interval
- RR interval buffer
- HRV/PRV metrics
- SpO2 AC/DC ratio + smoothing
- Respiratory rate dari downsampled PPG + FFT
- Signal quality

Entry point utamanya:

```cpp
SensorMetrics MetricsProcessor::processSamples(
  const RawSample* samples,
  int count,
  unsigned long windowEndMs
);
```

Input `samples` adalah array raw IR/Red seperti data dari MAX30102.

### `src/metrics_payload.cpp`

Berisi formatter payload JSON:

```cpp
std::string buildMetricsPayload(
  const SensorMetrics& metrics,
  const char* deviceId,
  const std::string& epochMsJson
);
```

Nilai metric invalid atau `NaN` akan dikirim sebagai `null`.

### `test/test_metrics_pipeline/test_main.cpp`

Unit test native dengan Unity. Test memakai data sintetis seperti output MAX30102:

- Sinyal PPG 75 BPM
- Respiratory modulation 0.25 Hz atau 15 breaths/min
- Finger off
- Sinyal lemah
- Payload JSON
- Mock publish MQTT boundary

## Alur Data

1. `TaskSensor` membaca FIFO MAX30102:

   ```cpp
   sample.ir = particleSensor.getFIFOIR();
   sample.red = particleSensor.getFIFORed();
   ```

2. Sample disimpan ke double buffer.

3. Setelah 500 sample terkumpul atau sekitar 5 detik pada 100 Hz, semaphore dikirim ke `TaskProcess`.

4. `TaskProcess` menyalin buffer siap proses, lalu memanggil:

   ```cpp
   metricsProcessor.processSamples(processBuffer, PROCESS_BUFFER_SIZE, millis());
   ```

5. Hasil `SensorMetrics` diformat menjadi JSON.

6. Payload dikirim ke MQTT topic:

   ```text
   sensor/wearable-001/metrics
   ```

## Format Payload MQTT

Contoh payload valid:

```json
{
  "deviceId": "WEARABLE-001",
  "timestamp": 123456,
  "epochMs": 1770000000000,
  "fingerDetected": true,
  "confidence": 3,
  "hr": 78.4,
  "spo2": 97.2,
  "respiratoryRate": 16.4,
  "hrv": {
    "meanRR": 769.2,
    "sdnn": 42.1,
    "rmssd": 35.8,
    "pnn50": 18.5
  }
}
```

Contoh payload invalid:

```json
{
  "deviceId": "WEARABLE-001",
  "timestamp": 42000,
  "epochMs": null,
  "fingerDetected": false,
  "confidence": 0,
  "hr": null,
  "spo2": null,
  "respiratoryRate": null,
  "hrv": {
    "meanRR": null,
    "sdnn": null,
    "rmssd": null,
    "pnn50": null
  }
}
```

`timestamp` adalah `millis()` sejak ESP32 menyala. `epochMs` diisi dari NTP jika WiFi berhasil sinkron waktu; jika belum valid, nilainya `null`.

## Confidence Level

`confidence` menunjukkan kualitas sinyal:

```text
0 = invalid
1 = poor
2 = fair
3 = good
```

Rekomendasi consumer:

- Pakai HR dan SpO2 hanya jika `fingerDetected=true` dan `confidence >= 2`.
- Pakai HRV dan respiratory rate hanya jika nilainya bukan `null`.
- Simpan payload invalid jika perlu audit/debug, tapi jangan diperlakukan sebagai data valid.

## Konfigurasi Sebelum Upload

Edit bagian berikut di `src/main.cpp`:

```cpp
const char* WIFI_SSID = "GANTI_DENGAN_SSID_WIFI";
const char* WIFI_PASSWORD = "GANTI_DENGAN_PASSWORD_WIFI";
const char* MQTT_BROKER = "192.168.1.10";
const int MQTT_PORT = 1883;
```

Jika topic atau device ID ingin diganti, edit di `include/metrics_types.h`:

```cpp
constexpr const char* DEVICE_ID = "WEARABLE-001";
constexpr const char* TOPIC_METRICS = "sensor/wearable-001/metrics";
```

## Build Firmware

Jalankan dari folder project:

```powershell
cd "D:\Code\Informatika\Semester 8\IOT\model\edge ai\RR & SpO2 processing"
pio run -e esp32-s3-devkitc-1
```

Jika berhasil, output akan berakhir dengan:

```text
SUCCESS
```

## Upload ke ESP32-S3

Hubungkan ESP32-S3 lewat USB, lalu jalankan:

```powershell
pio run -e esp32-s3-devkitc-1 -t upload
```

Jika port serial tidak otomatis terdeteksi, tambahkan `upload_port` di `platformio.ini`, contoh:

```ini
upload_port = COM5
```

## Serial Monitor

Untuk melihat debug output:

```powershell
pio device monitor -e esp32-s3-devkitc-1
```

Baud rate default:

```text
115200
```

Output debug akan berbentuk:

```text
HR: 78.4 | SpO2: 97.2 | RespRate: 16.4 | meanRR: 769.2 | SDNN: 42.1 | RMSSD: 35.8 | pNN50: 18.5 | Q: 3
```

## Run Unit Test Tanpa Hardware

Unit test native berjalan di PC dan tidak membutuhkan ESP32 atau MAX30102:

```powershell
pio test -e native
```

Expected output:

```text
native:test_metrics_pipeline [PASSED]
7 test cases: 7 succeeded
```

Test yang dicakup:

- HR/BPM dari PPG sintetis 75 BPM
- HRV valid setelah cukup RR interval
- HRV dan respiratory rate invalid saat data belum cukup
- Finger off menghasilkan metric invalid
- Sinyal lemah menurunkan confidence
- JSON payload angka dan `null`
- Mock publisher mengirim ke topic MQTT yang benar

## Run MQTT Consumer Contoh

Contoh subscriber Python:

```python
import json
import paho.mqtt.client as mqtt

def on_message(client, userdata, msg):
    data = json.loads(msg.payload.decode())

    valid = data["fingerDetected"] and data["confidence"] >= 2
    if not valid:
        print("Invalid signal:", data)
        return

    print("HR:", data["hr"])
    print("SpO2:", data["spo2"])
    print("RespRate:", data["respiratoryRate"])
    print("HRV:", data["hrv"])

client = mqtt.Client()
client.on_message = on_message
client.connect("192.168.1.10", 1883)
client.subscribe("sensor/wearable-001/metrics")
client.loop_forever()
```

## Apnea Inference di ESP32-S3

Firmware juga menjalankan model `apnea_detection` dari LightGBM yang sudah diekspor ke C++.

Model source yang dipakai:

```text
edge ai/prediction/apnea detection/models/apnea_detection_model.joblib
```

Generate ulang model C++ setelah retraining:

```powershell
cd "D:\Code\Informatika\Semester 8\IOT\model"
python "edge ai\RR & SpO2 processing\tools\export_apnea_model.py"
```

File generated:

```text
include/apnea_model_data.h
src/apnea_model_data.cpp
test/test_apnea_model/apnea_model_test_vectors.h
```

Output inference dikirim ke topic:

```text
sensor/wearable-001/inference/apnea
```

Contoh payload valid:

```json
{
  "deviceId": "WEARABLE-001",
  "timestamp": 60000,
  "epochMs": 1770000000000,
  "model": "apnea_detection",
  "windowSec": 60,
  "valid": true,
  "status": "ok",
  "prediction": 1,
  "label": "apnea",
  "apneaProbability": 0.8723,
  "confidence": 3
}
```

Sebelum window 60 detik siap atau sinyal belum cukup, `valid=false` dan `prediction`, `label`, serta `apneaProbability` bernilai `null`.

## Troubleshooting

### Build warning `I2C_BUFFER_LENGTH redefined`

Warning ini berasal dari library SparkFun MAX3010x dan Wire ESP32. Build tetap berhasil. Selama tidak ada error compile/link, warning ini bisa diabaikan.

### `project.checksum` muncul di `.pio/build`

Itu file cache internal PlatformIO. File ini sudah di-ignore lewat `.gitignore` karena folder `.pio/` tidak perlu dimasukkan ke version control.

### MQTT tidak terkirim

Cek:

- WiFi SSID/password benar.
- IP broker MQTT benar.
- Broker Mosquitto berjalan.
- ESP32 dan broker berada di network yang sama.
- Topic consumer adalah `sensor/wearable-001/metrics`.

### Metric sering `null`

Kemungkinan:

- Jari tidak menutupi sensor dengan stabil.
- Tekanan jari terlalu kuat atau terlalu lemah.
- Ambient light terlalu tinggi.
- Belum cukup data untuk HRV/RR.
- Respiratory rate membutuhkan sekitar 30 detik buffer PPG.

## Catatan Validasi

Unit test memastikan pipeline bisa memproses raw sample sintetis dan membuat payload dengan benar. Test ini tidak menggantikan validasi hardware, karena sinyal PPG asli sangat dipengaruhi posisi jari, noise, LED current, ambient light, dan karakteristik sensor.
