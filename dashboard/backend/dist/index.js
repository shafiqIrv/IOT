"use strict";
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
var __awaiter = (this && this.__awaiter) || function (thisArg, _arguments, P, generator) {
    function adopt(value) { return value instanceof P ? value : new P(function (resolve) { resolve(value); }); }
    return new (P || (P = Promise))(function (resolve, reject) {
        function fulfilled(value) { try { step(generator.next(value)); } catch (e) { reject(e); } }
        function rejected(value) { try { step(generator["throw"](value)); } catch (e) { reject(e); } }
        function step(result) { result.done ? resolve(result.value) : adopt(result.value).then(fulfilled, rejected); }
        step((generator = generator.apply(thisArg, _arguments || [])).next());
    });
};
Object.defineProperty(exports, "__esModule", { value: true });
const mqtt = __importStar(require("mqtt"));
const pg_1 = require("pg");
// Konfigurasi Database PostgreSQL
const pool = new pg_1.Pool({
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
        }
        else {
            console.error('Gagal subscribe:', err);
        }
    });
});
// Ambil angka jika valid (bukan null/undefined/NaN), selain itu kembalikan null
const num = (value) => {
    if (value === null || value === undefined)
        return null;
    const n = Number(value);
    return Number.isFinite(n) ? n : null;
};
// Turunkan status vital dari ambang batas HR/SpO2
const deriveAnomaly = (hr, spo2) => {
    if (spo2 < 92 || hr < 50 || hr > 120)
        return 'Kritis';
    return 'Normal';
};
const isMatch = (topic, suffix) => topic.endsWith(suffix);
client.on('message', (topic, message) => __awaiter(void 0, void 0, void 0, function* () {
    var _a, _b;
    try {
        const data = JSON.parse(message.toString());
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
            yield pool.query(query, values);
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
                (_a = num(data.epochMs)) !== null && _a !== void 0 ? _a : Date.now(),
                data.prediction,
                data.label,
                num(data.apneaProbability),
                data.windowSec,
                data.confidence,
            ];
            yield pool.query(query, values);
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
                (_b = num(data.epochMs)) !== null && _b !== void 0 ? _b : Date.now(),
                data.isSleeping,
                data.label,
                num(data.sleepProbability),
                data.windowSec,
            ];
            yield pool.query(query, values);
            console.log('[SLEEP] Data tersimpan:', data.label);
            return;
        }
        console.log('Topik tidak dikenali:', topic);
    }
    catch (error) {
        console.error('Gagal memproses pesan:', error);
    }
}));
