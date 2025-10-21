# models.py
import sqlite3
from pathlib import Path
import datetime as dt

DB_PATH = "database.db"

def connect():
    con = sqlite3.connect(DB_PATH, detect_types=sqlite3.PARSE_DECLTYPES)
    con.row_factory = sqlite3.Row
    return con

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
        cur.executemany(
            "INSERT INTO bins(id,name,capacity_kg) VALUES(?,?,?)",
            [(1,"Metal",5.0),(2,"Orgánico",5.0),(3,"Resto",5.0)]
        )
        cur.execute("""
        INSERT INTO config(id,steps_per_mm,v_max_mm_s,a_max_mm_s2,bin_positions_mm,threshold_percent)
        VALUES(1,2.5,120.0,400.0,'[0,120,240]',80)
        """)
        con.commit()
    con.close()

def get_bins():
    con = connect()
    cur = con.cursor()
    bins = cur.execute("SELECT id,name,capacity_kg FROM bins ORDER BY id").fetchall()
    out=[]
    for b in bins:
        grams = cur.execute("SELECT COALESCE(SUM(delta_g),0) FROM deposits WHERE bin=?",(b["id"],)).fetchone()[0] or 0
        pct = int(round((grams/(b["capacity_kg"]*1000.0))*100.0)) if b["capacity_kg"]>0 else 0
        out.append(dict(id=b["id"],name=b["name"],grams=grams,percent=pct,capacity_kg=b["capacity_kg"]))
    con.close()
    return out

def get_config():
    con = connect()
    row = con.execute("SELECT * FROM config WHERE id=1").fetchone()
    con.close()
    if not row: return {}
    return {
        "steps_per_mm": row["steps_per_mm"],
        "v_max_mm_s": row["v_max_mm_s"],
        "a_max_mm_s2": row["a_max_mm_s2"],
        "bin_positions_mm": eval(row["bin_positions_mm"] or "[0,120,240]"),
        "threshold_percent": row["threshold_percent"]
    }

def update_config(data:dict):
    cfg = get_config()
    for k in ["steps_per_mm","v_max_mm_s","a_max_mm_s2","threshold_percent"]:
        if k in data and data[k] is not None:
            cfg[k]=data[k]
    if "bin_positions_mm" in data:
        arr = data["bin_positions_mm"]
        if not (isinstance(arr,(list,tuple)) and len(arr)==3):
            raise ValueError("bin_positions_mm debe ser lista de 3 enteros")
        cfg["bin_positions_mm"]=[int(arr[0]),int(arr[1]),int(arr[2])]
    con = connect()
    con.execute("""
    UPDATE config SET steps_per_mm=?,v_max_mm_s=?,a_max_mm_s2=?,bin_positions_mm=?,threshold_percent=? WHERE id=1
    """,(cfg["steps_per_mm"],cfg["v_max_mm_s"],cfg["a_max_mm_s2"],str(cfg["bin_positions_mm"]),cfg["threshold_percent"]))
    con.commit(); con.close()

def insert_deposit(evt:dict):
    con = connect()
    con.execute("""
    INSERT INTO deposits(ts,bin,material,delta_g,fill_percent)
    VALUES(?,?,?,?,?)
    """,(evt["ts"],evt["bin"],evt["material"],evt["delta_g"],evt["fill_percent"]))
    con.commit(); con.close()

def get_recent_deposits(limit:int=20):
    con = connect()
    rows = con.execute("""
    SELECT ts,bin,material,delta_g,fill_percent
    FROM deposits ORDER BY id DESC LIMIT ?
    """,(limit,)).fetchall()
    con.close()
    return [dict(r) for r in rows][::-1]

def get_history(days:int=7):
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
    """,(f'-{days-1} day',)).fetchall()
    con.close()
    idx = {d:i for i,d in enumerate(labels)}
    for r in rows:
        d=r["d"]; b=str(r["bin"]); g=r["g"] or 0
        if d in idx and b in bins:
            bins[b][idx[d]] = int(g)
    total = [bins["1"][i]+bins["2"][i]+bins["3"][i] for i in range(days)]
    return {"labels":labels,"bins":bins,"total":total}
