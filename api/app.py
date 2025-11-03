# app.py
from flask import Flask, request, jsonify
from flask_cors import CORS
from flask_socketio import SocketIO
import datetime as dt
import models as m
import threading
import time

app = Flask(__name__)
CORS(app)
socketio = SocketIO(app, cors_allowed_origins="*")

# ---------------- Helpers ----------------
def ok(data=None, code=200):
    return jsonify({"ok": True, "data": data}), code

def err(msg, code=400):
    return jsonify({"ok": False, "error": msg}), code

# ---------------- Init DB ----------------
m.init_db()

# ---------------- Sistema de Comandos para Eje ----------------
PENDING_COMMANDS = {
    "jog_mm": None,
    "home": False
}

# ---------------- Bins / Config ----------------
@app.get("/api/bins")
def bins():
    return jsonify(m.get_bins())

@app.get("/api/config")
def get_cfg():
    config = m.get_config()
    # ✅ Obtener capacidad de los bins también para referencia
    bins_data = m.get_bins()
    config["bins_capacity_kg"] = [bin_data["capacity_kg"] for bin_data in bins_data]
    return jsonify(config)

@app.post("/api/config")
def set_cfg():
    data = request.get_json(force=True) or {}
    try:
        m.update_config(data)
        return ok({"saved": True})
    except Exception as e:
        return err(str(e), 400)

# ---------------- Deposits ----------------
@app.get("/api/deposits/recent")
def recent():
    limit = int(request.args.get("limit", 20))
    return jsonify(m.get_recent_deposits(limit))

@app.post("/api/deposits")
def add_deposit():
    j = request.get_json(force=True) or {}
    dg = j.get("delta_g", j.get("weight_grams", 0))
    evt = {
        "ts": j.get("ts") or dt.datetime.utcnow().isoformat(timespec="seconds") + "Z",
        "bin": int(j.get("bin", 0)),
        "material": str(j.get("material", "Desconocido")),
        "delta_g": int(dg),
        "fill_percent": int(j.get("fill_percent", 0)),
    }
    m.insert_deposit(evt)
    socketio.emit("deposit", evt, broadcast=True)
    return ok(evt, 201)

# ---------------- Histórico ----------------
@app.get("/api/deposits/history")
def history():
    days = int(request.args.get("days", 7))
    return ok(m.get_history(days))

# ---------------- Axis state (Realtime) ----------------
AXIS_STATE = {"state": "IDLE", "homed": True, "pos_mm": 0, "ts": None}

@app.get("/api/axis/state")
def axis_state_get():
    return ok(AXIS_STATE)

@app.post("/api/axis/state")
def axis_state_post():
    global AXIS_STATE
    j = request.get_json(force=True) or {}
    AXIS_STATE["state"] = j.get("state", AXIS_STATE["state"])
    AXIS_STATE["homed"] = bool(j.get("homed", AXIS_STATE["homed"]))
    AXIS_STATE["pos_mm"] = float(j.get("pos_mm", AXIS_STATE["pos_mm"]))
    AXIS_STATE["ts"] = j.get("ts") or dt.datetime.utcnow().isoformat(timespec="seconds") + "Z"
    socketio.emit("axis", AXIS_STATE, broadcast=True)
    return ok(AXIS_STATE, 202)

# ---------------- Comandos Jog/Home para Dashboard ----------------
@app.post("/api/axis/jog")
def axis_jog():
    """Recibe comandos de movimiento del dashboard"""
    data = request.get_json(force=True) or {}
    mm = data.get("mm", 0)
    
    print(f"📍 Comando JOG recibido: {mm} mm")
    
    # Actualizar estado del eje
    global AXIS_STATE
    AXIS_STATE["pos_mm"] += mm
    AXIS_STATE["state"] = "MOVING"
    AXIS_STATE["ts"] = dt.datetime.utcnow().isoformat(timespec="seconds") + "Z"
    
    # Emitir el nuevo estado
    socketio.emit("axis", AXIS_STATE, broadcast=True)
    
    return ok({"moved_mm": mm, "new_position": AXIS_STATE["pos_mm"]})

@app.post("/api/axis/home")  
def axis_home():
    """Recibe comandos de homing del dashboard"""
    global AXIS_STATE
    
    print("📍 Comando HOME recibido")
    
    # Simular proceso de homing
    AXIS_STATE["state"] = "HOMING"
    AXIS_STATE["homed"] = False
    AXIS_STATE["ts"] = dt.datetime.utcnow().isoformat(timespec="seconds") + "Z"
    socketio.emit("axis", AXIS_STATE, broadcast=True)
    
    # Simular que el homing se completó después de 2 segundos
    def complete_homing():
        time.sleep(2)
        AXIS_STATE["state"] = "IDLE"
        AXIS_STATE["homed"] = True
        AXIS_STATE["pos_mm"] = 0.0
        AXIS_STATE["ts"] = dt.datetime.utcnow().isoformat(timespec="seconds") + "Z"
        socketio.emit("axis", AXIS_STATE, broadcast=True)
        print("✅ Homing completado")
    
    # Ejecutar en segundo plano
    threading.Thread(target=complete_homing).start()
    
    return ok({"homed": True})

# ---------------- Sistema de Colas para ESP32 ----------------
@app.post("/api/axis/command")
def axis_command():
    """Endpoint para recibir comandos del dashboard (sistema de respaldo)"""
    data = request.get_json(force=True) or {}
    
    global PENDING_COMMANDS
    if "jog_mm" in data:
        PENDING_COMMANDS["jog_mm"] = data["jog_mm"]
        print(f"🔄 Comando JOG en cola: {data['jog_mm']} mm")
    if "home" in data:
        PENDING_COMMANDS["home"] = bool(data["home"])
        print("🔄 Comando HOME en cola")
    
    return ok({"received": True})

@app.get("/api/axis/pending_commands")
def get_pending_commands():
    """Endpoint para que el ESP32 consulte comandos pendientes"""
    global PENDING_COMMANDS
    
    commands = PENDING_COMMANDS.copy()
    # Limpiar comandos después de enviarlos
    PENDING_COMMANDS = {"jog_mm": None, "home": False}
    
    return ok({"commands": commands})

# ---------------- Config mínima para firmware ----------------
@app.get("/api/axis")
def axis_min():
    cfg = m.get_config()
    return jsonify({
        "steps_per_mm": cfg.get("steps_per_mm"),
        "vmax": cfg.get("v_max_mm_s"),
        "acc": cfg.get("a_max_mm_s2"),
        "positions_mm": cfg.get("bin_positions_mm"),
    })

# ---------------- Run ----------------
if __name__ == "__main__":
    socketio.run(app, host="0.0.0.0", port=5000, debug=True)