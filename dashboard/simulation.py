import paho.mqtt.client as mqtt
import json
import time
import random

# --- Configuration ---
BROKER = "localhost"
PORT = 1883
TOPIC = "wearable/data" # Must match the topic in your Node.js backend
DEVICE_ID = "WEARABLE-001"

# Initialize MQTT Client
client = mqtt.Client()

def connect_mqtt():
    try:
        client.connect(BROKER, PORT, 60)
        print(f"✅ Connected to MQTT Broker at {BROKER}:{PORT}")
    except Exception as e:
        print(f"❌ Connection failed: {e}")
        print("Make sure your docker-compose is running!")
        exit(1)

def simulate_data():
    connect_mqtt()
    print("🚀 Starting data simulation. Press Ctrl+C to stop.\n")

    try:
        while True:
            # Current time in milliseconds for epoch fields
            current_time_ms = int(time.time() * 1000)

            # 1. Generate Raw Vitals Data
            vitals_payload = {
                "model": "vitals", 
                "hr": random.randint(60, 110),
                "spo2": random.randint(94, 100),
                "anomaly": random.choice(["Normal", "Normal", "Normal", "Kritis"]),
                "stress": random.choice(["Rendah", "Sedang", "Tinggi"])
            }

            # 2. Generate Apnea Detection Data
            # Weighted to mostly send 'normal' (0) and occasionally 'apnea' (1)
            is_apnea = random.choices([0, 1], weights=[80, 20], k=1)[0]
            apnea_payload = {
                "deviceId": DEVICE_ID,
                "epochMs": current_time_ms,
                "model": "apnea_detection",
                "prediction": is_apnea,
                "label": "apnea" if is_apnea == 1 else "normal",
                "apneaProbability": round(random.uniform(0.60, 0.99), 2) if is_apnea == 1 else round(random.uniform(0.01, 0.30), 2),
                "windowSec": 60,
                "confidence": random.randint(1, 3)
            }

            # 3. Generate Sleep Detection Data
            is_sleeping = random.choice([0, 1])
            sleep_payload = {
                "deviceId": DEVICE_ID,
                "timestamp": current_time_ms,
                "model": "sleep_detection",
                "isSleeping": is_sleeping,
                "label": "Sleeping" if is_sleeping == 1 else "Awake",
                "sleepProbability": round(random.uniform(0.70, 0.99), 2),
                "windowSec": 30
            }

            # --- Publish Data to MQTT ---
            client.publish(TOPIC, json.dumps(vitals_payload))
            print(f"📡 [VITALS] Sent: {json.dumps(vitals_payload)}")

            client.publish(TOPIC, json.dumps(apnea_payload))
            print(f"📡 [APNEA]  Sent: {json.dumps(apnea_payload)}")

            client.publish(TOPIC, json.dumps(sleep_payload))
            print(f"📡 [SLEEP]  Sent: {json.dumps(sleep_payload)}")

            print("-" * 50)
            
            # Wait 3 seconds before sending the next batch
            time.sleep(3) 

    except KeyboardInterrupt:
        print("\n🛑 Simulation stopped by user.")
        client.disconnect()

if __name__ == "__main__":
    simulate_data()