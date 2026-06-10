import * as mqtt from 'mqtt';
import { Pool } from 'pg';
import { createServer } from 'http';
import { WebSocketServer, WebSocket } from 'ws';

const pool = new Pool({
  host: process.env.DB_HOST,
  port: parseInt(process.env.DB_PORT || '5432'),
  user: process.env.DB_USER,
  password: process.env.DB_PASSWORD,
  database: process.env.DB_NAME,
});

const mqttUrl = process.env.MQTT_BROKER_URL || 'mqtt://localhost:1883';
const client = mqtt.connect(mqttUrl);

const TOPIC_LIVE = 'sensor/+/live';
const TOPIC_EPOCH = 'sensor/+/epoch';
const TOPIC_APNEA = 'sensor/+/inference/apnea';
const TOPIC_SLEEP = 'sensor/+/inference/sleep';

// WebSocket server on port 4000
const httpServer = createServer();
const wss = new WebSocketServer({ server: httpServer });

wss.on('connection', (ws) => {
  console.log('[WS] Client terhubung, total:', wss.clients.size);
  ws.on('close', () => {
    console.log('[WS] Client terputus, total:', wss.clients.size);
  });
});

httpServer.listen(4000, () => {
  console.log('[WS] Server berjalan di port 4000');
});

client.on('connect', () => {
  console.log('Terhubung ke MQTT Broker');
  client.subscribe([TOPIC_LIVE, TOPIC_EPOCH, TOPIC_APNEA, TOPIC_SLEEP], (err) => {
    if (!err) {
      console.log(`Subscribed: ${TOPIC_LIVE}, ${TOPIC_EPOCH}, ${TOPIC_APNEA}, ${TOPIC_SLEEP}`);
    } else {
      console.error('Gagal subscribe:', err);
    }
  });
});

const num = (value: unknown): number | null => {
  if (value === null || value === undefined) return null;
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
};

const isMatch = (topic: string, suffix: string): boolean => topic.endsWith(suffix);

function cleanPayload(message: Buffer | string): string {
  const raw = Buffer.isBuffer(message) ? message.toString('utf8') : String(message);
  return raw.replace(/^\uFEFF/, '').replace(/\0/g, '').trim();
}

client.on('message', async (topic, message) => {
  try {
    const cleaned = cleanPayload(message);
    if (!cleaned) return;

    // Live data: relay to WebSocket clients, no DB write
    if (isMatch(topic, '/live')) {
      const payload = cleaned;
      wss.clients.forEach((ws) => {
        if (ws.readyState === WebSocket.OPEN) {
          ws.send(payload);
        }
      });
      return;
    }

    const data = JSON.parse(cleaned);

    // Epoch data: store in vitals_epochs
    if (isMatch(topic, '/epoch')) {
      const hr = data.hr || {};
      const spo2 = data.spo2 || {};
      const hrv = data.hrv || {};
      const rr = data.rr || {};
      const quality = data.quality || {};

      const epochStart = data.epochStart
        ? new Date(Number(data.epochStart))
        : new Date();
      const epochEnd = data.epochEnd
        ? new Date(Number(data.epochEnd))
        : new Date();

      const query = `
        INSERT INTO vitals_epochs
          (device_id, epoch_start, epoch_end,
           hr_mean, hr_min, hr_max, hr_std,
           spo2_mean, spo2_min, spo2_max, spo2_std, spo2_desat_count,
           hrv_sdnn, hrv_rmssd, hrv_pnn50, hrv_mean_rr,
           respiratory_rate_mean,
           valid_samples, total_samples, signal_quality_mean, finger_detected_pct)
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21)
      `;
      const values = [
        data.deviceId,
        epochStart,
        epochEnd,
        num(hr.mean),
        num(hr.min),
        num(hr.max),
        num(hr.std),
        num(spo2.mean),
        num(spo2.min),
        num(spo2.max),
        num(spo2.std),
        num(spo2.desatCount) ?? 0,
        num(hrv.sdnn),
        num(hrv.rmssd),
        num(hrv.pnn50),
        num(hrv.meanRR),
        num(rr.mean),
        num(quality.validSamples),
        num(quality.totalSamples),
        num(quality.meanConfidence),
        num(quality.fingerPct),
      ];
      await pool.query(query, values);
      console.log('[EPOCH] Data tersimpan');
      return;
    }

    // Apnea inference -> apnea_predictions
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

    // Sleep inference -> sleep_predictions
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
    const rawHex = Buffer.isBuffer(message) ? message.slice(0, 50).toString('hex') : '';
    const rawPreview = Buffer.isBuffer(message)
      ? message.slice(0, 100).toString('utf8')
      : String(message).slice(0, 100);
    console.error('Gagal memproses pesan:', error);
    console.error('[DEBUG] Topik:', topic);
    console.error('[DEBUG] Payload (preview):', JSON.stringify(rawPreview));
    console.error('[DEBUG] Payload (hex):', rawHex);
  }
});
