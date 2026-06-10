/*
 * ESP32-S3 + MAX30102 dual-stream processing pipeline
 * Live stream (~1s) for real-time monitoring + Epoch stream (60s) for analytics.
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <MAX30105.h>
#include <sys/time.h>
#include <time.h>

#include "apnea_features.h"
#include "epoch_accumulator.h"
#include "metrics_payload.h"
#include "metrics_processor.h"
#include "metrics_types.h"

MAX30105 particleSensor;

// ====== WIFI & MQTT CONFIG ======
const char *WIFI_SSID = "iPhone (7)";
const char *WIFI_PASSWORD = "pamparampampam";

const char *MQTT_BROKER = "172.20.10.3";
const int MQTT_PORT = 1883;

WiFiClient espClient;
PubSubClient mqtt(espClient);
MetricsProcessor metricsProcessor;
ApneaFeatureAccumulator apneaAccumulator;
EpochAccumulator epochAccumulator;

struct DoubleBuffer
{
    RawSample dataBuffer0[PROCESS_BUFFER_SIZE];
    RawSample dataBuffer1[PROCESS_BUFFER_SIZE];
    int activeBuffer;
    int bufferCounter;
    int readyBuffer;
    bool overrun;
};

TaskHandle_t TaskSensorHandle;
TaskHandle_t TaskProcessHandle;
SemaphoreHandle_t bufferReadySemaphore;
portMUX_TYPE sharedMux = portMUX_INITIALIZER_UNLOCKED;

DoubleBuffer dataBuffers = {};
unsigned long lastMqttAttemptMs = 0;

static std::string floatOrNull(float value, int decimals = 1);

std::string epochMsOrNull()
{
    timeval tv;
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec < 1700000000L)
    {
        return "null";
    }

    unsigned long long epochMs = ((unsigned long long)tv.tv_sec * 1000ULL) + (tv.tv_usec / 1000ULL);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%llu", epochMs);
    return std::string(buffer);
}

void printMetricsDebug(const SensorMetrics &metrics)
{
    Serial.printf(
        "HR: %.1f | SpO2: %.1f | RR: %.1f | Q: %d\n",
        metrics.bpm,
        metrics.spo2,
        metrics.respiratoryRate,
        metrics.confidence);
}

void sendLiveMQTT(const SensorMetrics &metrics)
{
    if (!mqtt.connected()) return;

    std::string tsStr = epochMsOrNull();

    std::string payload;
    payload.reserve(256);
    payload += "{\"deviceId\":\"";
    payload += DEVICE_ID;
    payload += "\",\"epochMs\":";
    payload += tsStr;
    payload += ",\"ts\":";
    payload += tsStr;
    payload += ",\"hr\":";
    payload += isValidMetric(metrics.bpm) ? floatOrNull(metrics.bpm) : "null";
    payload += ",\"spo2\":";
    payload += isValidMetric(metrics.spo2) ? floatOrNull(metrics.spo2) : "null";
    payload += ",\"respiratoryRate\":";
    payload += isValidMetric(metrics.respiratoryRate) ? floatOrNull(metrics.respiratoryRate) : "null";
    payload += ",\"rr\":";
    payload += isValidMetric(metrics.respiratoryRate) ? floatOrNull(metrics.respiratoryRate) : "null";
    payload += ",\"fingerDetected\":";
    payload += metrics.fingerDetected ? "true" : "false";
    payload += ",\"confidence\":";
    payload += std::to_string(metrics.confidence);
    payload += "}";

    mqtt.publish(TOPIC_LIVE, payload.c_str(), false);
}

static std::string floatOrNull(float value, int decimals) {
    if (!isValidMetric(value)) return "null";
    char buf[16];
    snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    return std::string(buf);
}

void sendEpochMQTT(const EpochStats &stats)
{
    if (!mqtt.connected()) return;

    std::string payload;
    payload.reserve(512);
    payload += "{\"deviceId\":\"";
    payload += DEVICE_ID;
    payload += "\",\"epochStart\":";
    payload += std::to_string(stats.epochStartMs);
    payload += ",\"epochEnd\":";
    payload += std::to_string(stats.epochEndMs);
    payload += ",\"hr\":{\"mean\":";
    payload += floatOrNull(stats.hrMean);
    payload += ",\"min\":";
    payload += floatOrNull(stats.hrMin);
    payload += ",\"max\":";
    payload += floatOrNull(stats.hrMax);
    payload += ",\"std\":";
    payload += floatOrNull(stats.hrStd);
    payload += "},\"spo2\":{\"mean\":";
    payload += floatOrNull(stats.spo2Mean);
    payload += ",\"min\":";
    payload += floatOrNull(stats.spo2Min);
    payload += ",\"max\":";
    payload += floatOrNull(stats.spo2Max);
    payload += ",\"std\":";
    payload += floatOrNull(stats.spo2Std);
    payload += ",\"desatCount\":";
    payload += std::to_string(stats.spo2DesatCount);
    payload += "},\"hrv\":{\"sdnn\":";
    payload += floatOrNull(stats.hrvSdnn);
    payload += ",\"rmssd\":";
    payload += floatOrNull(stats.hrvRmssd);
    payload += ",\"pnn50\":";
    payload += floatOrNull(stats.hrvPnn50);
    payload += ",\"meanRR\":";
    payload += floatOrNull(stats.hrvMeanRR);
    payload += "},\"rr\":{\"mean\":";
    payload += floatOrNull(stats.rrMean);
    payload += "},\"quality\":{\"validSamples\":";
    payload += std::to_string(stats.validSamples);
    payload += ",\"totalSamples\":";
    payload += std::to_string(stats.totalSamples);
    payload += ",\"meanConfidence\":";
    payload += floatOrNull(stats.meanConfidence);
    payload += ",\"fingerPct\":";
    payload += floatOrNull(stats.fingerDetectedPct);
    payload += "}}";

    mqtt.publish(TOPIC_EPOCH, payload.c_str(), false);
    Serial.println("[MQTT] Epoch terkirim");
}

void sendApneaMQTT(const ApneaInference &inference)
{
    if (!mqtt.connected()) return;

    std::string payload = buildApneaPayload(inference, DEVICE_ID, epochMsOrNull());
    mqtt.publish(TOPIC_APNEA, payload.c_str(), false);
    Serial.println("[MQTT] Apnea inference terkirim");
}

void connectWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Menghubungkan ke WiFi");
    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 20000UL)
    {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print("\nWiFi terhubung, IP: ");
        Serial.println(WiFi.localIP());
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    }
    else
    {
        Serial.println("\nWiFi belum terhubung, sensor tetap berjalan.");
    }
}

void maintainMQTT()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        WiFi.reconnect();
        return;
    }

    if (mqtt.connected())
    {
        mqtt.loop();
        return;
    }

    if (millis() - lastMqttAttemptMs < 5000UL)
    {
        return;
    }

    lastMqttAttemptMs = millis();
    Serial.print("Menghubungkan ke Mosquitto...");
    if (mqtt.connect(DEVICE_ID))
    {
        Serial.println("Terhubung!");
    }
    else
    {
        Serial.print("Gagal, rc=");
        Serial.println(mqtt.state());
    }
}

void storeRawSample(const RawSample &sample)
{
    bool shouldSignal = false;

    portENTER_CRITICAL(&sharedMux);
    int active = dataBuffers.activeBuffer;
    int counter = dataBuffers.bufferCounter;

    if (active == 0)
    {
        dataBuffers.dataBuffer0[counter] = sample;
    }
    else
    {
        dataBuffers.dataBuffer1[counter] = sample;
    }
    dataBuffers.bufferCounter++;

    if (dataBuffers.bufferCounter >= PROCESS_BUFFER_SIZE)
    {
        if (dataBuffers.readyBuffer == -1)
        {
            dataBuffers.readyBuffer = active;
            dataBuffers.activeBuffer = 1 - active;
            shouldSignal = true;
        }
        else
        {
            dataBuffers.overrun = true;
        }
        dataBuffers.bufferCounter = 0;
    }
    portEXIT_CRITICAL(&sharedMux);

    if (shouldSignal)
    {
        xSemaphoreGive(bufferReadySemaphore);
    }
}

void copyReadyBuffer(RawSample *destination, bool &copied)
{
    copied = false;

    portENTER_CRITICAL(&sharedMux);
    int ready = dataBuffers.readyBuffer;
    if (ready == 0)
    {
        memcpy(destination, dataBuffers.dataBuffer0, sizeof(dataBuffers.dataBuffer0));
        copied = true;
    }
    else if (ready == 1)
    {
        memcpy(destination, dataBuffers.dataBuffer1, sizeof(dataBuffers.dataBuffer1));
        copied = true;
    }
    dataBuffers.readyBuffer = -1;
    portEXIT_CRITICAL(&sharedMux);
}

void TaskSensor(void *pvParameters)
{
    Serial.print("Task Sensor berjalan di Core: ");
    Serial.println(xPortGetCoreID());

    for (;;)
    {
        particleSensor.check();

        while (particleSensor.available())
        {
            RawSample sample;
            sample.ir = particleSensor.getFIFOIR();
            sample.red = particleSensor.getFIFORed();
            particleSensor.nextSample();

            storeRawSample(sample);
        }

        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

void TaskProcess(void *pvParameters)
{
    Serial.print("Task Proses & MQTT berjalan di Core: ");
    Serial.println(xPortGetCoreID());

    static RawSample processBuffer[PROCESS_BUFFER_SIZE];

    for (;;)
    {
        maintainMQTT();

        if (xSemaphoreTake(bufferReadySemaphore, 100 / portTICK_PERIOD_MS) == pdTRUE)
        {
            bool copied = false;
            copyReadyBuffer(processBuffer, copied);
            if (!copied) continue;

            unsigned long now = millis();
            SensorMetrics metrics = metricsProcessor.processSamples(processBuffer, PROCESS_BUFFER_SIZE, now);

            // 1. Publish live stream (~every 1s)
            printMetricsDebug(metrics);
            sendLiveMQTT(metrics);

            // 2. Feed epoch accumulator
            epochAccumulator.addMetrics(metrics);

            // 3. Feed apnea accumulator
            float recentRRIntervals[RECENT_RR_INTERVALS_SIZE];
            int recentRRIntervalCount = metricsProcessor.copyRecentRRIntervals(
                recentRRIntervals, RECENT_RR_INTERVALS_SIZE);
            apneaAccumulator.addMetrics(metrics, recentRRIntervals, recentRRIntervalCount);

            // 4. Check if epoch is ready (60s)
            if (epochAccumulator.isReady(now))
            {
                EpochStats stats = epochAccumulator.buildAndReset(now);
                if (stats.ready)
                {
                    sendEpochMQTT(stats);
                }
            }

            // 5. Apnea inference (runs every cycle, only produces valid after 60s)
            ApneaInference apneaInference = apneaAccumulator.predict(metrics.timestamp, metrics.confidence);
            if (apneaInference.valid)
            {
                sendApneaMQTT(apneaInference);
            }
        }
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2000);

    dataBuffers.activeBuffer = 0;
    dataBuffers.bufferCounter = 0;
    dataBuffers.readyBuffer = -1;
    dataBuffers.overrun = false;
    metricsProcessor.reset();
    apneaAccumulator.reset();
    epochAccumulator.reset();

    connectWiFi();
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setBufferSize(1024);

    Wire.begin();
    if (!particleSensor.begin(Wire, I2C_SPEED_FAST))
    {
        Serial.println("MAX3010x tidak ditemukan!");
        while (1) { delay(1000); }
    }

    particleSensor.setup(60, 4, 2, SAMPLE_RATE, 411, 4096);
    particleSensor.clearFIFO();

    bufferReadySemaphore = xSemaphoreCreateBinary();
    if (bufferReadySemaphore == nullptr)
    {
        Serial.println("Gagal membuat semaphore.");
        while (1) { delay(1000); }
    }

    xTaskCreatePinnedToCore(TaskSensor, "SensorTask", 10000, nullptr, 1, &TaskSensorHandle, 0);
    xTaskCreatePinnedToCore(TaskProcess, "ProcessTask", 32000, nullptr, 1, &TaskProcessHandle, 1);

    Serial.println("Sistem RTOS siap (dual-stream: live 1s + epoch 60s).");
}

void loop()
{
    vTaskDelete(nullptr);
}
