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

function evaluateWarnings(epoch: {
  hr_mean: number | null;
  spo2_mean: number | null;
  spo2_min: number | null;
  hrv_rmssd: number | null;
  hrv_sdnn: number | null;
}): VitalWarning[] {
  const warnings: VitalWarning[] = [];
  const hr = epoch.hr_mean;
  const spo2Min = epoch.spo2_min;
  const rmssd = epoch.hrv_rmssd;
  const sdnn = epoch.hrv_sdnn;

  if (hr !== null) {
    if (hr < 40 || hr > 120) {
      warnings.push({
        metric: 'HR',
        level: 'critical',
        message: hr < 40 ? `Bradikardia berat: ${round(hr, 1)} BPM` : `Takikardia berat: ${round(hr, 1)} BPM`,
        value: hr,
      });
    } else if (hr < 50 || hr > 100) {
      warnings.push({
        metric: 'HR',
        level: 'warning',
        message: hr < 50 ? `Bradikardia: ${round(hr, 1)} BPM` : `Takikardia: ${round(hr, 1)} BPM`,
        value: hr,
      });
    }
  }

  if (spo2Min !== null) {
    if (spo2Min < 88) {
      warnings.push({
        metric: 'SpO2',
        level: 'critical',
        message: `Hipoksemia berat: SpO2 ${round(spo2Min, 1)}%`,
        value: spo2Min,
      });
    } else if (spo2Min < 92) {
      warnings.push({
        metric: 'SpO2',
        level: 'warning',
        message: `SpO2 rendah: ${round(spo2Min, 1)}%`,
        value: spo2Min,
      });
    }
  }

  if (rmssd !== null) {
    if (rmssd < 10) {
      warnings.push({
        metric: 'HRV',
        level: 'critical',
        message: `RMSSD sangat rendah: ${round(rmssd, 1)} ms`,
        value: rmssd,
      });
    } else if (rmssd < 20) {
      warnings.push({
        metric: 'HRV',
        level: 'warning',
        message: `RMSSD rendah: ${round(rmssd, 1)} ms`,
        value: rmssd,
      });
    }
  }

  if (sdnn !== null) {
    if (sdnn < 30) {
      warnings.push({
        metric: 'SDNN',
        level: 'critical',
        message: `SDNN sangat rendah: ${round(sdnn, 1)} ms`,
        value: sdnn,
      });
    } else if (sdnn < 50) {
      warnings.push({
        metric: 'SDNN',
        level: 'warning',
        message: `SDNN rendah: ${round(sdnn, 1)} ms`,
        value: sdnn,
      });
    }
  }

  return warnings;
}

export async function GET() {
  try {
    // Latest epochs (last ~2 hours for trend)
    const epochsRes = await pool.query(`
      SELECT * FROM vitals_epochs
      ORDER BY epoch_end DESC LIMIT 120
    `);
    const epochs = epochsRes.rows;

    // Sleep predictions
    const sleepRes = await pool.query(`
      SELECT * FROM sleep_predictions
      ORDER BY received_at DESC LIMIT 60
    `);
    const sleepRows = sleepRes.rows;

    // Apnea predictions
    const apneaRes = await pool.query(`
      SELECT * FROM apnea_predictions
      ORDER BY received_at DESC LIMIT 60
    `);
    const apneaRows = apneaRes.rows;

    // --- AHI ---
    const sleepCountRes = await pool.query(`
      SELECT COUNT(*) as cnt, MIN(window_sec) as ws
      FROM sleep_predictions
      WHERE is_sleeping = 1 AND received_at > NOW() - INTERVAL '8 hours'
    `);
    const sleepSamples = parseInt(sleepCountRes.rows[0].cnt);
    const windowSec = parseInt(sleepCountRes.rows[0].ws) || 30;
    const sleepHours = (sleepSamples * windowSec) / 3600;

    const apneaCountRes = await pool.query(`
      SELECT COUNT(*) as cnt
      FROM apnea_predictions
      WHERE prediction = 1 AND received_at > NOW() - INTERVAL '8 hours'
    `);
    const apneaEventsCount = parseInt(apneaCountRes.rows[0].cnt);
    const ahiValue = sleepHours >= 0.083 ? apneaEventsCount / sleepHours : null;
    const ahiSeverity = ahiValue !== null ? classifyAhiSeverity(ahiValue) : null;

    // --- ODI from epoch spo2_min ---
    const desatRes = await pool.query(`
      SELECT COALESCE(SUM(spo2_desat_count), 0) as total_desats
      FROM vitals_epochs
      WHERE epoch_end > NOW() - INTERVAL '8 hours'
    `);
    const totalDesats = parseInt(desatRes.rows[0].total_desats);
    const odiValue = sleepHours >= 0.083 ? totalDesats / sleepHours : null;
    const odiSeverity = odiValue !== null ? classifyOdiSeverity(odiValue) : null;

    // --- Warnings from latest epoch ---
    const latestEpoch = epochs[0] || null;
    const warnings: VitalWarning[] = latestEpoch ? evaluateWarnings(latestEpoch) : [];

    // --- Trend data (chronological) ---
    const trend = epochs
      .slice()
      .reverse()
      .map((e) => ({
        time: timeLabel(e.epoch_end),
        hr: round(e.hr_mean, 1),
        spo2: round(e.spo2_mean, 1),
        rr: round(e.respiratory_rate_mean, 1),
        hrv_rmssd: round(e.hrv_rmssd, 1),
        hrv_sdnn: round(e.hrv_sdnn, 1),
      }));

    // --- Sleep history ---
    const sleepHistory = sleepRows
      .slice()
      .reverse()
      .map((row) => ({
        time: timeLabel(row.received_at),
        isSleeping: row.is_sleeping,
      }));

    const responseData = {
      latestEpoch: latestEpoch
        ? {
            hrMean: round(latestEpoch.hr_mean, 1),
            hrMin: round(latestEpoch.hr_min, 1),
            hrMax: round(latestEpoch.hr_max, 1),
            spo2Mean: round(latestEpoch.spo2_mean, 1),
            spo2Min: round(latestEpoch.spo2_min, 1),
            spo2Max: round(latestEpoch.spo2_max, 1),
            hrvRmssd: round(latestEpoch.hrv_rmssd, 1),
            hrvSdnn: round(latestEpoch.hrv_sdnn, 1),
            rrMean: round(latestEpoch.respiratory_rate_mean, 1),
            quality: round(latestEpoch.finger_detected_pct, 0),
            lastUpdate: timeLabel(latestEpoch.epoch_end),
          }
        : null,
      ahi: {
        value: ahiValue !== null ? round(ahiValue, 1) : null,
        severity: ahiSeverity,
        eventsCount: apneaEventsCount,
        sleepHours: round(sleepHours, 2),
      },
      odi: {
        value: odiValue !== null ? round(odiValue, 1) : null,
        severity: odiSeverity,
        eventsCount: totalDesats,
        sleepHours: round(sleepHours, 2),
      },
      warnings,
      trend,
      sleepHistory,
    };

    return NextResponse.json(responseData);
  } catch (error) {
    console.error('Analytics DB Error:', error);
    return NextResponse.json({ error: 'Gagal mengambil data analytics' }, { status: 500 });
  }
}
