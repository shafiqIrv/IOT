-- Tabel untuk data vitals (metrics dari ESP32 edge)
CREATE TABLE IF NOT EXISTS patient_vitals (
    id SERIAL PRIMARY KEY,
    heart_rate REAL NOT NULL,
    spo2 REAL NOT NULL,
    respiratory_rate REAL,
    hrv_mean_rr REAL,
    hrv_sdnn REAL,
    hrv_rmssd REAL,
    hrv_pnn50 REAL,
    anomaly_status VARCHAR(20), -- Normal / Kritis (diturunkan di backend dari ambang batas)
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
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
