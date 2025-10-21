# app.py
from flask import Flask, request, jsonify
from flask_cors import CORS
from flask_socketio import SocketIO
import datetime as dt
import models as m

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

# ---------------- Bins / Config ----------------
@app.get("/api/bins")
def bins():
    return jsonify(m.get_bins())

@app.get("/api/config")
def get_cfg():
    return jsonify(m.get_config())

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
    socketio.emit("deposit", evt, broadcast=True)  # tiempo real
    return ok(evt, 201)

# ---------------- Histórico ----------------
@app.get("/api/deposits/history")
def history():
    days = int(request.args.get("days", 7))
    return ok(m.get_history(days))

# ---------------- Axis state (Realtime) ----------------
# Se guarda en memoria (suficiente para dashboard); si querés persistir, pásalo a DB.
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
    socketio.emit("axis", AXIS_STATE, broadcast=True)  # empuja al dashboard
    return ok(AXIS_STATE, 202)

# ---------------- Config mínima para firmware (opcional) ----------------
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
    # usar eventlet para WS
    socketio.run(app, host="0.0.0.0", port=5000)
