import { NextResponse } from 'next/server';
import { Pool } from 'pg';

const pool = new Pool({
  host: process.env.DB_HOST || 'localhost',
  port: parseInt(process.env.DB_PORT || '5432'),
  user: process.env.DB_USER || 'admin',
  password: process.env.DB_PASSWORD || 'adminpassword',
  database: process.env.DB_NAME || 'health_db',
});

const round = (value: unknown, digits = 0): number | null => {
  if (value === null || value === undefined) return null;
  const n = Number(value);
  if (!Number.isFinite(n)) return null;
  const factor = 10 ** digits;
  return Math.round(n * factor) / factor;
};

const timeLabel = (value: number | string | Date) =>
  new Date(value).toLocaleTimeString('id-ID', {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  });

type WarningLevel = 'warning' | 'critical';

interface VitalWarning {
  metric: string;
  level: WarningLevel;
  message: string;
  value: number;
}

function classifyAhiSeverity(ahi: number): string {
  if (ahi < 5) return 'Normal';
  if (ahi < 15) return 'Ringan';
  if (ahi < 30) return 'Sedang';
  return 'Berat';
}

function classifyOdiSeverity(odi: number): string {
  if (odi < 5) return 'Normal';
  if (odi < 15) return 'Ringan';
  if (odi < 30) return 'Sedang';
  return 'Berat';
}

function computeODI(spo2Values: number[], sleepHours: number): { value: number; eventsCount: number } {
  const BASELINE_WINDOW = 24;
  const DROP_THRESHOLD = 3;
  const MIN_DURATION = 2;

  if (spo2Values.length < BASELINE_WINDOW + MIN_DURATION) {
    return { value: 0, eventsCount: 0 };
  }

  let events = 0;
  let consecutiveDrops = 0;
  let eventCounted = false;

  for (let i = BASELINE_WINDOW; i < spo2Values.length; i++) {
    let sum = 0;
    for (let j = i - BASELINE_WINDOW; j < i; j++) {
      sum += spo2Values[j];
    }
    const baseline = sum / BASELINE_WINDOW;
    const current = spo2Values[i];

    if (baseline - current >= DROP_THRESHOLD) {
      consecutiveDrops++;
      if (consecutiveDrops >= MIN_DURATION && !eventCounted) {
        events++;
        eventCounted = true;
      }
    } else {
      consecutiveDrops = 0;
      eventCounted = false;
    }
  }

  const value = sleepHours > 0 ? events / sleepHours : 0;
  return { value, eventsCount: events };
}

function evaluateWarnings(vital: {
  heart_rate: number;
  spo2: number;
  hrv_rmssd: number | null;
  hrv_sdnn: number | null;
}): VitalWarning[] {
  const warnings: VitalWarning[] = [];
  const hr = vital.heart_rate;
  const spo2 = vital.spo2;
  const rmssd = vital.hrv_rmssd;
  const sdnn = vital.hrv_sdnn;

  if (hr < 40 || hr > 120) {
    warnings.push({
      metric: 'HR',
      level: 'critical',
      message: hr < 40 ? `Bradikardia berat: ${hr} BPM` : `Takikardia berat: ${hr} BPM`,
      value: hr,
    });
  } else if (hr < 50 || hr > 100) {
    warnings.push({
      metric: 'HR',
      level: 'warning',
      message: hr < 50 ? `Bradikardia: ${hr} BPM` : `Takikardia: ${hr} BPM`,
      value: hr,
    });
  }

  if (spo2 < 88) {
    warnings.push({
      metric: 'SpO2',
      level: 'critical',
      message: `Hipoksemia berat: SpO2 ${spo2}%`,
      value: spo2,
    });
  } else if (spo2 < 92) {
    warnings.push({
      metric: 'SpO2',
      level: 'warning',
      message: `SpO2 rendah: ${spo2}%`,
      value: spo2,
    });
  }

  if (rmssd !== null) {
    if (rmssd < 10) {
      warnings.push({
        metric: 'HRV',
        level: 'critical',
        message: `RMSSD sangat rendah: ${rmssd} ms`,
        value: rmssd,
      });
    } else if (rmssd < 20) {
      warnings.push({
        metric: 'HRV',
        level: 'warning',
        message: `RMSSD rendah: ${rmssd} ms`,
        value: rmssd,
      });
    }
  }

  if (sdnn !== null) {
    if (sdnn < 30) {
      warnings.push({
        metric: 'SDNN',
        level: 'critical',
        message: `SDNN sangat rendah: ${sdnn} ms`,
        value: sdnn,
      });
    } else if (sdnn < 50) {
      warnings.push({
        metric: 'SDNN',
        level: 'warning',
        message: `SDNN rendah: ${sdnn} ms`,
        value: sdnn,
      });
    }
  }

  return warnings;
}

export async function GET() {
  try {
    const vitalsQuery = await pool.query(
      'SELECT * FROM patient_vitals ORDER BY timestamp DESC LIMIT 20'
    );
    const vitalsData = vitalsQuery.rows;

    const sleepQuery = await pool.query(
      'SELECT * FROM sleep_predictions ORDER BY received_at DESC LIMIT 30'
    );
    const sleepRows = sleepQuery.rows;
    const latestSleep = sleepRows[0];

    const apneaQuery = await pool.query(
      'SELECT * FROM apnea_predictions ORDER BY received_at DESC LIMIT 1'
    );
    const latestApnea = apneaQuery.rows[0];

    if (vitalsData.length === 0) {
      return NextResponse.json({ error: 'Tidak ada data vitals' }, { status: 404 });
    }

    const latestVital = vitalsData[0];

    // --- AHI Computation ---
    const sleepCountRes = await pool.query(`
      SELECT COUNT(*) as cnt, MIN(window_sec) as ws
      FROM sleep_predictions
      WHERE is_sleeping = 1 AND received_at > NOW() - INTERVAL '1 hour'
    `);
    const sleepSamples = parseInt(sleepCountRes.rows[0].cnt);
    const windowSec = parseInt(sleepCountRes.rows[0].ws) || 30;
    const sleepHours = (sleepSamples * windowSec) / 3600;

    const apneaCountRes = await pool.query(`
      SELECT COUNT(*) as cnt
      FROM apnea_predictions
      WHERE prediction = 1 AND received_at > NOW() - INTERVAL '1 hour'
    `);
    const apneaEventsCount = parseInt(apneaCountRes.rows[0].cnt);

    // Require at least 5 minutes of sleep to compute meaningful AHI
    const ahiValue = sleepHours >= 0.083 ? apneaEventsCount / sleepHours : null;
    const ahiSeverity = ahiValue !== null ? classifyAhiSeverity(ahiValue) : null;

    // --- ODI Computation ---
    const spo2Res = await pool.query(`
      SELECT spo2 FROM patient_vitals
      WHERE timestamp > NOW() - INTERVAL '1 hour'
      ORDER BY timestamp ASC
    `);
    const spo2Values = spo2Res.rows.map((r: { spo2: number }) => r.spo2);
    const odiResult = computeODI(spo2Values, sleepHours);
    const odiValue = sleepHours >= 0.083 ? odiResult.value : null;
    const odiSeverity = odiValue !== null ? classifyOdiSeverity(odiValue) : null;

    // --- Warnings ---
    const warnings = evaluateWarnings(latestVital);

    const responseData = {
      latest: {
        heartRate: round(latestVital.heart_rate, 1),
        spo2: round(latestVital.spo2, 1),
        respiratoryRate: round(latestVital.respiratory_rate, 1),
        isCritical: latestVital.anomaly_status === 'Kritis',
        lastUpdate: timeLabel(latestVital.timestamp),
        hrv: {
          meanRR: round(latestVital.hrv_mean_rr, 1),
          sdnn: round(latestVital.hrv_sdnn, 1),
          rmssd: round(latestVital.hrv_rmssd, 1),
          pnn50: round(latestVital.hrv_pnn50, 1),
        },
        sleepStatus: latestSleep ? latestSleep.label : 'Menunggu data...',
        sleepProb: latestSleep ? Math.round(latestSleep.sleep_probability * 100) : 0,
        apneaWarning: latestApnea ? latestApnea.prediction === 1 : false,
        apneaProb: latestApnea ? Math.round(latestApnea.apnea_probability * 100) : 0,
      },
      history: vitalsData
        .slice()
        .reverse()
        .map((row) => ({
          time: timeLabel(row.timestamp),
          hr: round(row.heart_rate, 1),
          spo2: round(row.spo2, 1),
          rmssd: round(row.hrv_rmssd, 1),
          sdnn: round(row.hrv_sdnn, 1),
          pnn50: round(row.hrv_pnn50, 1),
        })),
      sleepHistory: sleepRows
        .slice()
        .reverse()
        .map((row) => ({
          time: timeLabel(row.received_at),
          isSleeping: row.is_sleeping,
        })),
      ahi: {
        value: ahiValue !== null ? round(ahiValue, 1) : null,
        severity: ahiSeverity,
        eventsCount: apneaEventsCount,
        sleepHours: round(sleepHours, 2),
      },
      odi: {
        value: odiValue !== null ? round(odiValue, 1) : null,
        severity: odiSeverity,
        eventsCount: odiResult.eventsCount,
        sleepHours: round(sleepHours, 2),
      },
      warnings,
    };

    return NextResponse.json(responseData);
  } catch (error) {
    console.error('Database Error:', error);
    return NextResponse.json({ error: 'Gagal mengambil data dari database' }, { status: 500 });
  }
}
