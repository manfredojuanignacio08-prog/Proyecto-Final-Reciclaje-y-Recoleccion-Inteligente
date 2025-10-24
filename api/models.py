# models.py
import sqlite3
from pathlib import Path
import datetime as dt
import json

DB_PATH = "database.db"

def connect():
    con = sqlite3.connect(DB_PATH, detect_types=sqlite3.PARSE_DECLTYPES)
    con.row_factory = sqlite3.Row
    con.execute("PRAGMA foreign_keys = ON;")
    return con

def _ensure_three_bins(cur):
    """
    Garantiza exactamente 3 bins (1..3) y elimina restos de versiones anteriores.
    """
    # Eliminar posibles residuos del 4º tacho o 'Plástico/Vidrio'
    cur.execute("DELETE FROM bins WHERE name = 'Plástico/Vidrio'")
    cur.execute("DELETE FROM bins WHERE id NOT IN (1,2,3)")
    # Reasegurar 3 bins correctos
    bins_target = [
        (1, "Metal",    5.0),
        (2, "Orgánico", 5.0),
        (3, "Resto",    5.0),
    ]
    for b in bins_target:
        cur.execute(
            "INSERT INTO bins(id,name,capacity_kg) VALUES(?,?,?) "
            "ON CONFLICT(id) DO UPDATE SET name=excluded.name, capacity_kg=excluded.capacity_kg",
            b
        )

def init_db():
    fresh = not Path(DB_PATH).exists()
    con = connect()
    cur = con.cursor()
    cur.executescript("""
    CREATE TABLE IF NOT EXISTS bins(
      id INTEGER PRIMARY KEY,
      name TEXT NOT NULL,
      capacity_kg REAL NOT NULL
    );
    CREATE TABLE IF NOT EXISTS config(
      id INTEGER PRIMARY KEY CHECK(id=1),
      steps_per_mm REAL,
      v_max_mm_s REAL,
      a_max_mm_s2 REAL,
      bin_positions_mm TEXT,
      threshold_percent INTEGER DEFAULT 80
    );
    CREATE TABLE IF NOT EXISTS deposits(
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      ts TEXT NOT NULL,
      bin INTEGER NOT NULL,
      material TEXT NOT NULL,
      delta_g INTEGER NOT NULL,
      fill_percent INTEGER NOT NULL
    );
    """)
    con.commit()

    if fresh:
        # Carga inicial
        cur.executemany(
            "INSERT INTO bins(id,name,capacity_kg) VALUES(?,?,?)",
            [(1,"Metal",5.0),(2,"Orgánico",5.0),(3,"Resto",5.0)]
        )
        cur.execute("""
        INSERT INTO config(id,steps_per_mm,v_max_mm_s,a_max_mm_s2,bin_positions_mm,threshold_percent)
        VALUES(1,2.5,120.0,400.0,?,80)
        """, (json.dumps([0,120,240]),))
        con.commit()
    else:
        # Micro-migración: asegurar 3 bins y una fila de config
        _ensure_three_bins(cur)
        cur.execute("""
        INSERT INTO config(id,steps_per_mm,v_max_mm_s,a_max_mm_s2,bin_positions_mm,threshold_percent)
        VALUES(1,2.5,120.0,400.0,?,80)
        ON CONFLICT(id) DO NOTHING
        """, (json.dumps([0,120,240]),))
        con.commit()

    con.close()

def get_bins():
    """
    Devuelve lista de bins:
    [{id,name,grams,percent,capacity_kg}, ...]
    """
    con = connect()
    cur = con.cursor()
    bins = cur.execute("SELECT id,name,capacity_kg FROM bins ORDER BY id").fetchall()
    out=[]
    for b in bins:
        grams = cur.execute(
            "SELECT COALESCE(SUM(delta_g),0) FROM deposits WHERE bin=?",
            (b["id"],)
        ).fetchone()[0] or 0
        pct = int(round((grams/(b["capacity_kg"]*1000.0))*100.0)) if b["capacity_kg"]>0 else 0
        out.append(dict(
            id=b["id"],
            name=b["name"],
            grams=int(grams),
            percent=pct,
            capacity_kg=float(b["capacity_kg"])
        ))
    con.close()
    return out

def get_config():
    """
    Devuelve:
    {
      steps_per_mm, v_max_mm_s, a_max_mm_s2,
      bin_positions_mm: [x1,x2,x3],
      threshold_percent
    }
    """
    con = connect()
    row = con.execute("SELECT * FROM config WHERE id=1").fetchone()
    con.close()
    if not row:
        return {
            "steps_per_mm": 2.5,
            "v_max_mm_s": 120.0,
            "a_max_mm_s2": 400.0,
            "bin_positions_mm": [0,120,240],
            "threshold_percent": 80
        }
    try:
        positions = json.loads(row["bin_positions_mm"]) if row["bin_positions_mm"] else [0,120,240]
    except Exception:
        positions = [0,120,240]
    return {
        "steps_per_mm": row["steps_per_mm"],
        "v_max_mm_s": row["v_max_mm_s"],
        "a_max_mm_s2": row["a_max_mm_s2"],
        "bin_positions_mm": positions,
        "threshold_percent": row["threshold_percent"]
    }

def update_config(data:dict):
    """
    Actualiza parcialmente la config. Valida tamaños y tipos.
    """
    cfg = get_config()

    if "steps_per_mm" in data and data["steps_per_mm"] is not None:
        cfg["steps_per_mm"] = float(data["steps_per_mm"])

    if "v_max_mm_s" in data and data["v_max_mm_s"] is not None:
        cfg["v_max_mm_s"] = float(data["v_max_mm_s"])

    if "a_max_mm_s2" in data and data["a_max_mm_s2"] is not None:
        cfg["a_max_mm_s2"] = float(data["a_max_mm_s2"])

    if "threshold_percent" in data and data["threshold_percent"] is not None:
        tp = int(data["threshold_percent"])
        if tp < 10: tp = 10
        if tp > 95: tp = 95
        cfg["threshold_percent"] = tp

    if "bin_positions_mm" in data:
        arr = data["bin_positions_mm"]
        if not (isinstance(arr,(list,tuple)) and len(arr)==3):
            raise ValueError("bin_positions_mm debe ser lista de 3 enteros")
        cfg["bin_positions_mm"] = [int(arr[0]), int(arr[1]), int(arr[2])]

    con = connect()
    con.execute("""
    UPDATE config
       SET steps_per_mm=?,
           v_max_mm_s=?,
           a_max_mm_s2=?,
           bin_positions_mm=?,
           threshold_percent=?
     WHERE id=1
    """, (
        cfg["steps_per_mm"],
        cfg["v_max_mm_s"],
        cfg["a_max_mm_s2"],
        json.dumps(cfg["bin_positions_mm"]),
        cfg["threshold_percent"]
    ))
    con.commit(); con.close()

def insert_deposit(evt:dict):
    """
    Inserta un evento de depósito. Espera:
    { ts, bin, material, delta_g, fill_percent }
    """
    con = connect()
    con.execute("""
    INSERT INTO deposits(ts,bin,material,delta_g,fill_percent)
    VALUES(?,?,?,?,?)
    """, (evt["ts"], evt["bin"], evt["material"], evt["delta_g"], evt["fill_percent"]))
    con.commit(); con.close()

def get_recent_deposits(limit:int=20):
    """
    Devuelve lista (array) para la tabla del dashboard:
    [{ts,bin,material,delta_g,fill_percent}, ...]
    """
    con = connect()
    rows = con.execute("""
    SELECT ts,bin,material,delta_g,fill_percent
      FROM deposits
     ORDER BY id DESC
     LIMIT ?
    """, (limit,)).fetchall()
    con.close()
    return [dict(r) for r in rows][::-1]  # cronológico ascendente

def get_history(days:int=7):
    """
    Agregados por día y por bin para gráfico histórico.
    Devuelve: {"labels":[...],"bins":{"1":[..],"2":[..],"3":[..]},"total":[..]}
    """
    con = connect()
    cur = con.cursor()
    today = dt.date.today()
    labels = [(today - dt.timedelta(days=i)).isoformat() for i in range(days-1,-1,-1)]
    bins = {"1":[0]*days, "2":[0]*days, "3":[0]*days}
    rows = cur.execute("""
      SELECT substr(ts,1,10) AS d, bin, COALESCE(SUM(delta_g),0) AS g
        FROM deposits
       WHERE DATE(substr(ts,1,10)) >= DATE('now', ?)
    GROUP BY d, bin
    """, (f'-{days-1} day',)).fetchall()
    con.close()
    idx = {d:i for i,d in enumerate(labels)}
    for r in rows:
        d = r["d"]; b = str(r["bin"]); g = r["g"] or 0
        if d in idx and b in bins:
            bins[b][idx[d]] = int(g)
    total = [bins["1"][i] + bins["2"][i] + bins["3"][i] for i in range(days)]
    return {"labels": labels, "bins": bins, "total": total}
