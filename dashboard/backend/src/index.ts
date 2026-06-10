import * as mqtt from 'mqtt';
import { Pool } from 'pg';

// Konfigurasi Database PostgreSQL
const pool = new Pool({
  host: process.env.DB_HOST,
  port: parseInt(process.env.DB_PORT || '5432'),
  user: process.env.DB_USER,
  password: process.env.DB_PASSWORD,
  database: process.env.DB_NAME,
});

// Koneksi ke MQTT Broker
const mqttUrl = process.env.MQTT_BROKER_URL || 'mqtt://localhost:1883';
const client = mqtt.connect(mqttUrl);

// Topik nyata dari ESP32 edge (metrics + apnea) dan cloud sleep service
const TOPIC_METRICS = 'sensor/+/metrics';
const TOPIC_APNEA = 'sensor/+/inference/apnea';
const TOPIC_SLEEP = 'sensor/+/inference/sleep';

client.on('connect', () => {
  console.log('Terhubung ke MQTT Broker');
  client.subscribe([TOPIC_METRICS, TOPIC_APNEA, TOPIC_SLEEP], (err) => {
    if (!err) {
      console.log(`Subscribed ke topik: ${TOPIC_METRICS}, ${TOPIC_APNEA}, ${TOPIC_SLEEP}`);
    } else {
      console.error('Gagal subscribe:', err);
    }
  });
});

// Ambil angka jika valid (bukan null/undefined/NaN), selain itu kembalikan null
const num = (value: unknown): number | null => {
  if (value === null || value === undefined) return null;
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
};

// Turunkan status vital dari ambang batas HR/SpO2
const deriveAnomaly = (hr: number, spo2: number): string => {
  if (spo2 < 92 || hr < 50 || hr > 120) return 'Kritis';
  return 'Normal';
};

const isMatch = (topic: string, suffix: string): boolean => topic.endsWith(suffix);

client.on('message', async (topic, message) => {
  try {
    const raw = Buffer.isBuffer(message)
      ? message.toString('utf8')
      : String(message);

    // Strip UTF-8 BOM and null bytes that ESP32/PubSubClient may inject
    const cleaned = raw.replace(/^\uFEFF/, '').replace(/\0/g, '').trim();

    if (!cleaned) {
      console.warn('[SKIP] Payload kosong pada topik:', topic);
      return;
    }

    const data = JSON.parse(cleaned);

    // 1. Metrics (HR, SpO2, RR, HRV) -> patient_vitals
    if (isMatch(topic, '/metrics')) {
      const hr = num(data.hr);
      const spo2 = num(data.spo2);

      // Lewati frame tanpa jari terdeteksi / sinyal tidak valid
      if (hr === null || spo2 === null) {
        return;
      }

      const hrv = data.hrv || {};
      const query = `
        INSERT INTO patient_vitals
          (heart_rate, spo2, respiratory_rate, hrv_mean_rr, hrv_sdnn, hrv_rmssd, hrv_pnn50, anomaly_status)
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
      `;
      const values = [
        hr,
        spo2,
        num(data.respiratoryRate),
        num(hrv.meanRR),
        num(hrv.sdnn),
        num(hrv.rmssd),
        num(hrv.pnn50),
        deriveAnomaly(hr, spo2),
      ];
      await pool.query(query, values);
      console.log('[VITALS] Data tersimpan');
      return;
    }

    // 2. Apnea inference -> apnea_predictions
    if (isMatch(topic, '/inference/apnea')) {
      if (data.valid !== true) {
        console.log('[APNEA] Frame tidak valid dilewati:', data.status);
        return;
      }
      const query = `
        INSERT INTO apnea_predictions
          (device_id, epoch_ms, prediction, label, apnea_probability, window_sec, confidence)
        VALUES ($1, $2, $3, $4, $5, $6, $7)
      `;
      const values = [
        data.deviceId,
        num(data.epochMs) ?? Date.now(),
        data.prediction,
        data.label,
        num(data.apneaProbability),
        data.windowSec,
        data.confidence,
      ];
      await pool.query(query, values);
      console.log('[APNEA] Data tersimpan:', data.label);
      return;
    }

    // 3. Sleep inference -> sleep_predictions
    if (isMatch(topic, '/inference/sleep')) {
      if (data.valid !== true) {
        console.log('[SLEEP] Frame tidak valid dilewati:', data.status);
        return;
      }
      const query = `
        INSERT INTO sleep_predictions
          (device_id, timestamp_ms, is_sleeping, label, sleep_probability, window_sec)
        VALUES ($1, $2, $3, $4, $5, $6)
      `;
      const values = [
        data.deviceId,
        num(data.epochMs) ?? Date.now(),
        data.isSleeping,
        data.label,
        num(data.sleepProbability),
        data.windowSec,
      ];
      await pool.query(query, values);
      console.log('[SLEEP] Data tersimpan:', data.label);
      return;
    }

    console.log('Topik tidak dikenali:', topic);
  } catch (error) {
    const rawHex = Buffer.isBuffer(message)
      ? message.slice(0, 50).toString('hex')
      : '';
    const rawPreview = Buffer.isBuffer(message)
      ? message.slice(0, 100).toString('utf8')
      : String(message).slice(0, 100);
    console.error('Gagal memproses pesan:', error);
    console.error('[DEBUG] Topik:', topic);
    console.error('[DEBUG] Payload (preview):', JSON.stringify(rawPreview));
    console.error('[DEBUG] Payload (hex):', rawHex);
  }
});
