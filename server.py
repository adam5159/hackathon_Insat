"""
server.py — ReTeqFusion Part 1 receiver
Listens on the Bluetooth COM port (or USB serial), parses JSON payloads,
stores them, and exposes a REST endpoint so Part 2 pipeline can pull data.
Run: python server.py --port COM6 --baud 9600
"""
import serial
import json
import threading
import argparse
from datetime import datetime
from flask import Flask, jsonify
from collections import deque

app = Flask(__name__)
readings = deque(maxlen=10000)   # in-memory ring buffer
lock = threading.Lock()

# ── REST endpoints ────────────────────────────────────────────────────────────

@app.get("/readings")
def get_readings():
    with lock:
        return jsonify(list(readings))

@app.get("/latest")
def get_latest():
    with lock:
        if not readings:
            return jsonify({"error": "no data yet"}), 404
        return jsonify(readings[-1])

@app.get("/health")
def health():
    with lock:
        return jsonify({
            "status": "ok",
            "records": len(readings),
            "last_ts": readings[-1].get("ts_ms") if readings else None
        })

# ── Serial reader thread ──────────────────────────────────────────────────────

def read_serial(port: str, baud: int):
    print(f"[SERVER] Opening {port} at {baud} baud...")
    while True:
        try:
            ser = serial.Serial(port, baud, timeout=2)
            print(f"[SERVER] Connected. Flask API: http://localhost:5000")
            print(f"[SERVER] Endpoints: /readings  /latest  /health")
            while True:
                try:
                    line = ser.readline().decode("utf-8", errors="ignore").strip()
                    if not line or not line.startswith("{"):
                        continue
                    payload = json.loads(line)
                    payload["server_ts"] = datetime.utcnow().isoformat()
                    with lock:
                        readings.append(payload)
                    print(f"[RX] {payload.get('device_id')} "
                          f"T={payload.get('sensors', {}).get('temperature_C')} "
                          f"H={payload.get('sensors', {}).get('humidity_pct')} "
                          f"I={payload.get('sensors', {}).get('current_mA')} "
                          f"P={payload.get('sensors', {}).get('power_W')} "
                          f"valid={payload.get('quality', {}).get('all_valid')}")
                except json.JSONDecodeError:
                    pass   # non-JSON line — ignore
                except Exception as e:
                    print(f"[WARN] Serial read error: {e}")
                    break  # reconnect outer loop

            ser.close()

        except serial.SerialException as e:
            print(f"[ERROR] {port} not available: {e}")
            print(f"[SERVER] Retrying in 5 seconds...")
            import time
            time.sleep(5)

# ── main ──────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port",       default="COM6",  help="Serial/BT COM port")
    parser.add_argument("--baud",       default=9600,    type=int)
    parser.add_argument("--host",       default="0.0.0.0")
    parser.add_argument("--flask-port", default=5000,    type=int)
    args = parser.parse_args()

    t = threading.Thread(
        target=read_serial,
        args=(args.port, args.baud),
        daemon=True
    )
    t.start()

    app.run(host=args.host, port=args.flask_port, debug=False)