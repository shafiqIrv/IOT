-- Legacy vitals table (kept for backward compat, no longer inserted)
CREATE TABLE IF NOT EXISTS patient_vitals (
    id SERIAL PRIMARY KEY,
    heart_rate REAL NOT NULL,
    spo2 REAL NOT NULL,
    respiratory_rate REAL,
    hrv_mean_rr REAL,
    hrv_sdnn REAL,
    hrv_rmssd REAL,
    hrv_pnn50 REAL,
    anomaly_status VARCHAR(20),
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 60-second epoch summaries from dual-stream architecture
CREATE TABLE IF NOT EXISTS vitals_epochs (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,
    epoch_start TIMESTAMP NOT NULL,
    epoch_end TIMESTAMP NOT NULL,
    hr_mean REAL,
    hr_min REAL,
    hr_max REAL,
    hr_std REAL,
    spo2_mean REAL,
    spo2_min REAL,
    spo2_max REAL,
    spo2_std REAL,
    spo2_desat_count INT DEFAULT 0,
    hrv_sdnn REAL,
    hrv_rmssd REAL,
    hrv_pnn50 REAL,
    hrv_mean_rr REAL,
    respiratory_rate_mean REAL,
    valid_samples INT,
    total_samples INT,
    signal_quality_mean REAL,
    finger_detected_pct REAL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Tabel untuk data model Apnea (inference dari ESP32 edge)
CREATE TABLE IF NOT EXISTS apnea_predictions (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,
    epoch_ms BIGINT NOT NULL,
    prediction INT NOT NULL,
    label VARCHAR(50),
    apnea_probability FLOAT,
    window_sec INT,
    confidence INT,
    received_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Tabel untuk data model Sleep Detection (inference dari cloud service)
CREATE TABLE IF NOT EXISTS sleep_predictions (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,
    timestamp_ms BIGINT NOT NULL,
    is_sleeping INT NOT NULL,
    label VARCHAR(50),
    sleep_probability FLOAT,
    window_sec INT,
    received_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
