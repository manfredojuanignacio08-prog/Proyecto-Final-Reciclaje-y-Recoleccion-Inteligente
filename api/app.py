# app.py - VERSIÓN COMPLETA CON PESOS Y CONFIG ESP32
from flask import Flask, request, jsonify
from flask_cors import CORS
from flask_socketio import SocketIO
import datetime as dt
import models as m
import threading
import time
import random

app = Flask(__name__)
CORS(app)

socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading')

# ---------------- Init DB ----------------
m.init_db()

# ---------------- Datos de Prueba para el Gráfico ----------------
def create_sample_history_data():
    """Crea datos de ejemplo para el gráfico histórico"""
    today = dt.date.today()
    labels = [(today - dt.timedelta(days=i)).isoformat() for i in range(6, -1, -1)]
    
    return {
        "labels": labels,
        "bins": {
            "1": [random.randint(50, 200) for _ in range(7)],  # Metal
            "2": [random.randint(100, 300) for _ in range(7)], # Orgánico  
            "3": [random.randint(80, 250) for _ in range(7)]   # Resto
        },
        "total": [random.randint(300, 600) for _ in range(7)]  # Total
    }

# ---------------- Sistema de Comandos para Eje ----------------
PENDING_COMMANDS = {
    "jog_mm": None,
    "home": False
}

# ---------------- Weights (NUEVO - para reporte periódico de pesos) ----------------
@app.post("/api/weights")
def receive_weights():
    """
    Recibe los pesos de los tachos desde el ESP32
    Ejemplo: {"weight1": 150, "weight2": 300, "weight3": 75}
    """
    data = request.get_json(force=True) or {}
    print(f"⚖️ Pesos recibidos - T1: {data.get('weight1', 0)}g, T2: {data.get('weight2', 0)}g, T3: {data.get('weight3', 0)}g")
    
    # Emitir por WebSocket para actualizar el frontend en tiempo real
    socketio.emit("weights_update", data, broadcast=True)
    
    return jsonify({"received": True, "timestamp": dt.datetime.utcnow().isoformat()})

# ---------------- Config extendida (NUEVO - para parámetros del ESP32) ----------------
@app.get("/api/config/esp32")
def get_esp32_config():
    """
    Configuración específica para el ESP32
    Incluye parámetros de clasificación que no están en la config general
    """
    config = m.get_config()
    
    # Configuración extendida para el ESP32
    esp32_config = {
        "steps_per_mm": config.get("steps_per_mm", 2.5),
        "v_max_mm_s": config.get("v_max_mm_s", 120.0),
        "a_max_mm_s2": config.get("a_max_mm_s2", 400.0),
        "bin_positions_mm": config.get("bin_positions_mm", [0, 120, 240]),
        "alert_threshold": config.get("threshold_percent", 50),
        "t_dh": 3.0,  # Δ% humedad para orgánicos
        "t_dt": 1.0,  # Δ°C para orgánicos  
        "t_min_g": 10  # Δg mínimo para confirmar depósito
    }
    
    return jsonify(esp32_config)

# ---------------- Bins / Config ----------------
@app.get("/api/bins")
def bins():
    bins_data = m.get_bins()
    print(f"📦 Enviando {len(bins_data)} tachos")
    return jsonify(bins_data)

@app.get("/api/config")
def get_cfg():
    config = m.get_config()
    bins_data = m.get_bins()
    config["bins_capacity_kg"] = [bin_data["capacity_kg"] for bin_data in bins_data]
    return jsonify(config)

@app.post("/api/config")
def set_cfg():
    data = request.get_json(force=True) or {}
    try:
        m.update_config(data)
        return jsonify({"saved": True})
    except Exception as e:
        return jsonify({"error": str(e)}), 400

# ---------------- Deposits ----------------
@app.get("/api/deposits/recent")
def recent():
    limit = int(request.args.get("limit", 20))
    deposits = m.get_recent_deposits(limit)
    print(f"📊 Enviando {len(deposits)} depósitos recientes")
    return jsonify(deposits)

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
    
    print(f"📥 Depósito registrado: Tacho {evt['bin']} - {evt['material']} - {evt['delta_g']}g")
    
    return jsonify(evt), 201

# ---------------- Histórico ----------------
@app.get("/api/deposits/history")
def history():
    days = int(request.args.get("days", 7))
    try:
        # Primero intenta obtener datos reales
        real_data = m.get_history(days)
        if real_data and real_data.get("labels"):
            print(f"📈 Enviando datos históricos reales ({days} días)")
            return jsonify(real_data)
        else:
            # Si no hay datos reales, usa datos de muestra
            print(f"📈 Enviando datos históricos de muestra ({days} días)")
            return jsonify(create_sample_history_data())
    except Exception as e:
        print(f"⚠️  Error con datos históricos, usando muestra: {e}")
        return jsonify(create_sample_history_data())

# ---------------- Axis state (Realtime) ----------------
AXIS_STATE = {"state": "IDLE", "homed": True, "pos_mm": 0, "ts": None}

@app.get("/api/axis/state")
def axis_state_get():
    return jsonify(AXIS_STATE)

@app.post("/api/axis/state")
def axis_state_post():
    global AXIS_STATE
    j = request.get_json(force=True) or {}
    AXIS_STATE["state"] = j.get("state", AXIS_STATE["state"])
    AXIS_STATE["homed"] = bool(j.get("homed", AXIS_STATE["homed"]))
    AXIS_STATE["pos_mm"] = float(j.get("pos_mm", AXIS_STATE["pos_mm"]))
    AXIS_STATE["ts"] = j.get("ts") or dt.datetime.utcnow().isoformat(timespec="seconds") + "Z"
    socketio.emit("axis", AXIS_STATE, broadcast=True)
    return jsonify(AXIS_STATE)

# ---------------- Comandos Jog/Home para Dashboard ----------------
@app.post("/api/axis/jog")
def axis_jog():
    data = request.get_json(force=True) or {}
    mm = data.get("mm", 0)
    
    print(f"📍 Comando JOG recibido: {mm} mm")
    
    global AXIS_STATE
    AXIS_STATE["pos_mm"] += mm
    AXIS_STATE["state"] = "MOVING"
    AXIS_STATE["ts"] = dt.datetime.utcnow().isoformat(timespec="seconds") + "Z"
    
    socketio.emit("axis", AXIS_STATE, broadcast=True)
    
    return jsonify({"moved_mm": mm, "new_position": AXIS_STATE["pos_mm"]})

@app.post("/api/axis/home")  
def axis_home():
    global AXIS_STATE
    
    print("📍 Comando HOME recibido")
    
    AXIS_STATE["state"] = "HOMING"
    AXIS_STATE["homed"] = False
    AXIS_STATE["ts"] = dt.datetime.utcnow().isoformat(timespec="seconds") + "Z"
    socketio.emit("axis", AXIS_STATE, broadcast=True)
    
    def complete_homing():
        time.sleep(2)
        AXIS_STATE["state"] = "IDLE"
        AXIS_STATE["homed"] = True
        AXIS_STATE["pos_mm"] = 0.0
        AXIS_STATE["ts"] = dt.datetime.utcnow().isoformat(timespec="seconds") + "Z"
        socketio.emit("axis", AXIS_STATE, broadcast=True)
        print("✅ Homing completado")
    
    threading.Thread(target=complete_homing).start()
    
    return jsonify({"homed": True})

# ---------------- Sistema de Colas para ESP32 ----------------
@app.post("/api/axis/command")
def axis_command():
    data = request.get_json(force=True) or {}
    
    global PENDING_COMMANDS
    if "jog_mm" in data:
        PENDING_COMMANDS["jog_mm"] = data["jog_mm"]
        print(f"🔄 Comando JOG en cola: {data['jog_mm']} mm")
    if "home" in data:
        PENDING_COMMANDS["home"] = bool(data["home"])
        print("🔄 Comando HOME en cola")
    
    return jsonify({"received": True})

@app.get("/api/axis/pending_commands")
def get_pending_commands():
    global PENDING_COMMANDS
    
    commands = PENDING_COMMANDS.copy()
    PENDING_COMMANDS = {"jog_mm": None, "home": False}
    
    return jsonify({"commands": commands})

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

# ---------------- Health Check ----------------
@app.get("/api/health")
def health_check():
    return jsonify({"status": "healthy", "timestamp": dt.datetime.utcnow().isoformat()})

@app.get("/")
def home():
    return jsonify({
        "message": "🚀 Administrador de Basura Inteligente - API funcionando", 
        "endpoints": {
            "bins": "/api/bins",
            "config": "/api/config", 
            "config_esp32": "/api/config/esp32",
            "weights": "/api/weights (POST)",
            "axis_state": "/api/axis/state",
            "history": "/api/deposits/history",
            "recent_deposits": "/api/deposits/recent"
        }
    })

# ---------------- Run ----------------
if __name__ == "__main__":
    print("=" * 60)
    print("🚀 ADMINISTRADOR DE BASURA INTELIGENTE - BACKEND INICIADO")
    print("=" * 60)
    print("📍 URL Base: http://localhost:5000")
    print("📊 Dashboard: http://localhost:8000")
    print("")
    print("📋 Endpoints principales:")
    print("   GET  /api/bins                 - Estado de tachos")
    print("   GET  /api/config               - Configuración general") 
    print("   GET  /api/config/esp32         - Configuración ESP32")
    print("   POST /api/weights              - Reportar pesos (ESP32)")
    print("   POST /api/deposits             - Registrar depósito")
    print("   GET  /api/deposits/history     - Datos históricos (gráfico)")
    print("   GET  /api/deposits/recent      - Depósitos recientes")
    print("   POST /api/axis/jog             - Mover eje")
    print("   POST /api/axis/home            - Homing")
    print("   GET  /api/health               - Estado del sistema")
    print("")
    print("🔌 Endpoints para ESP32:")
    print("   POST /api/weights              - Enviar pesos periódicos")
    print("   GET  /api/config/esp32         - Obtener configuración")
    print("   POST /api/deposits             - Reportar depósitos")
    print("   GET  /api/axis/pending_commands- Comandos pendientes")
    print("")
    print("🌐 ACCESO DESDE RED LOCAL:")
    print("   📱 Dispositivos móviles: http://TU_IP_LOCAL:8000")
    print("   💻 Computadora: http://localhost:8000")
    print("=" * 60)
    
    socketio.run(app, host="0.0.0.0", port=5000, debug=True)