# Roletta Greenhouse Monitor — Panel Dashboard
# Subscribes to HiveMQ Cloud MQTT, stores in SQLite, serves via Hugging Face

import panel as pn
import paho.mqtt.client as mqtt
import sqlite3
import json
import threading
from datetime import datetime

import pandas as pd
import plotly.express as px

pn.extension("plotly")

# ────────────────────────────────────────────────
# Configuration
# ────────────────────────────────────────────────
MQTT_HOST  = "757e9f308248406eb752d49b9fde3249.s1.eu.hivemq.cloud"  # ← your HiveMQ cluster URL
MQTT_PORT  = 8883
MQTT_USER  = "roletta_dashboard"                 # ← dashboard MQTT username
MQTT_PASS  = "Putin2023"               # ← dashboard MQTT password
TOPIC_SUB  = "greenhouse/sensors"
TOPIC_CMD  = "greenhouse/cmd"
DB_PATH    = "greenhouse.db"

# ────────────────────────────────────────────────
# SQLite setup
# ────────────────────────────────────────────────
def init_db():
    con = sqlite3.connect(DB_PATH)
    con.execute("""
        CREATE TABLE IF NOT EXISTS readings (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            ts         TEXT    NOT NULL,
            temp       REAL,
            humidity   REAL,
            soil_pct   INTEGER,
            ldr        INTEGER,
            pump       INTEGER,
            fan        INTEGER,
            motion     INTEGER,
            alarm      INTEGER,
            spotlight  INTEGER
        )
    """)
    con.commit()
    con.close()

def insert_reading(d: dict):
    con = sqlite3.connect(DB_PATH)
    con.execute("""
        INSERT INTO readings
            (ts, temp, humidity, soil_pct, ldr, pump, fan, motion, alarm, spotlight)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    """, (
        datetime.utcnow().isoformat(),
        d.get("temp"), d.get("humidity"), d.get("soil_pct"), d.get("ldr"),
        d.get("pump"), d.get("fan"),
        d.get("motion"), d.get("alarm"), d.get("spotlight")
    ))
    con.commit()
    con.close()

def load_history(hours: int = 1) -> pd.DataFrame:
    con = sqlite3.connect(DB_PATH)
    df = pd.read_sql(
        f"""SELECT ts, temp, humidity, soil_pct, ldr
            FROM readings
            WHERE ts >= datetime('now', '-{hours} hours')
            ORDER BY ts""",
        con
    )
    con.close()
    return df

def latest_row() -> dict:
    con = sqlite3.connect(DB_PATH)
    cur = con.execute("""
        SELECT temp, humidity, soil_pct, pump, fan, motion, alarm, spotlight
        FROM readings ORDER BY id DESC LIMIT 1
    """)
    row = cur.fetchone()
    con.close()
    if row:
        keys = ["temp", "humidity", "soil_pct", "pump", "fan", "motion", "alarm", "spotlight"]
        return dict(zip(keys, row))
    return {}

# ────────────────────────────────────────────────
# Panel widgets
# ────────────────────────────────────────────────
temp_gauge  = pn.indicators.Gauge(
    name="Temperature °C", value=0, bounds=(0, 50),
    colors=[(0.6, "green"), (0.8, "orange"), (1.0, "red")]
)
hum_gauge   = pn.indicators.Gauge(
    name="Humidity %", value=0, bounds=(0, 100),
    colors=[(0.7, "green"), (1.0, "red")]
)
soil_gauge  = pn.indicators.Gauge(
    name="Soil Moisture %", value=0, bounds=(0, 100),
    colors=[(0.3, "red"), (0.7, "orange"), (1.0, "green")]
)
alarm_ind   = pn.indicators.BooleanStatus(value=False, color="danger",  name="⚠ Alarm")
motion_ind  = pn.indicators.BooleanStatus(value=False, color="warning", name="🚶 Motion")
pump_ind    = pn.indicators.BooleanStatus(value=False, color="success", name="💧 Pump")
fan_ind     = pn.indicators.BooleanStatus(value=False, color="primary", name="🌀 Fan")
spotlight_ind = pn.indicators.BooleanStatus(value=False, color="warning", name="💡 Spotlight")

pump_btn    = pn.widgets.Toggle(name="Toggle Pump", button_type="success", width=150)
fan_btn     = pn.widgets.Toggle(name="Toggle Fan",  button_type="primary", width=150)
history_sel = pn.widgets.Select(
    name="History window", options=[1, 6, 12, 24], value=1, width=150
)
plot_pane   = pn.pane.Plotly(height=320, sizing_mode="stretch_width")
status_text = pn.pane.Markdown("**Status:** Waiting for data...", width=400)

# ────────────────────────────────────────────────
# MQTT client (runs in background thread)
# ────────────────────────────────────────────────
mq = mqtt.Client(client_id="panel_dashboard", transport="tcp")
mq.username_pw_set(MQTT_USER, MQTT_PASS)
mq.tls_set()  # uses system CA store — works with HiveMQ Cloud certificates

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Dashboard MQTT connected")
        client.subscribe(TOPIC_SUB)
    else:
        print(f"MQTT connection failed: rc={rc}")

def on_message(client, userdata, msg):
    try:
        d = json.loads(msg.payload.decode())
    except json.JSONDecodeError:
        return

    insert_reading(d)

    # Update live indicators (Panel is thread-safe for param updates)
    temp_gauge.value    = round(float(d.get("temp", 0)), 1)
    hum_gauge.value     = round(float(d.get("humidity", 0)), 1)
    soil_gauge.value    = int(d.get("soil_pct", 0))
    alarm_ind.value     = bool(d.get("alarm", 0))
    motion_ind.value    = bool(d.get("motion", 0))
    pump_ind.value      = bool(d.get("pump", 0))
    fan_ind.value       = bool(d.get("fan", 0))
    spotlight_ind.value = bool(d.get("spotlight", 0))
    status_text.object  = f"**Last update:** {datetime.utcnow().strftime('%H:%M:%S UTC')}"

def on_disconnect(client, userdata, rc):
    print(f"MQTT disconnected rc={rc}, attempting reconnect...")
    try:
        client.reconnect()
    except Exception as e:
        print(f"Reconnect failed: {e}")

mq.on_connect    = on_connect
mq.on_message    = on_message
mq.on_disconnect = on_disconnect

def send_command(cmd: str):
    payload = json.dumps({"cmd": cmd})
    mq.publish(TOPIC_CMD, payload, qos=1)
    print(f"Sent command: {cmd}")

def pump_cb(event):
    send_command("pump_on" if event.new else "pump_off")

def fan_cb(event):
    send_command("fan_on" if event.new else "fan_off")

pump_btn.param.watch(pump_cb, "value")
fan_btn.param.watch(fan_cb,  "value")

# ────────────────────────────────────────────────
# Plot update
# ────────────────────────────────────────────────
def update_plot(event=None):
    df = load_history(history_sel.value)
    if df.empty:
        plot_pane.object = None
        return
    df_melt = df.melt(
        id_vars="ts",
        value_vars=["temp", "humidity", "soil_pct"],
        var_name="Sensor",
        value_name="Reading"
    )
    sensor_labels = {"temp": "Temp (°C)", "humidity": "Humidity (%)", "soil_pct": "Soil (%)"}
    df_melt["Sensor"] = df_melt["Sensor"].map(sensor_labels)

    fig = px.line(
        df_melt, x="ts", y="Reading", color="Sensor",
        title=f"Sensor History — Last {history_sel.value}h",
        template="plotly_white",
        labels={"ts": "Time (UTC)"}
    )
    fig.update_layout(legend=dict(orientation="h", y=-0.2))
    plot_pane.object = fig

history_sel.param.watch(update_plot, "value")

# ────────────────────────────────────────────────
# Periodic plot refresh (every 10 s)
# ────────────────────────────────────────────────
pn.state.add_periodic_callback(update_plot, period=10000)

# ────────────────────────────────────────────────
# Layout
# ────────────────────────────────────────────────
controls = pn.Column(
    "## 🎛 Controls",
    pn.Row(pump_btn, fan_btn),
    "## ⏱ History",
    history_sel,
    width=200
)

indicators = pn.Column(
    "## 📊 Live Readings",
    pn.Row(temp_gauge, hum_gauge, soil_gauge),
    "## 🔔 Status",
    pn.Row(alarm_ind, motion_ind, pump_ind, fan_ind, spotlight_ind),
    status_text
)

dashboard = pn.template.FastListTemplate(
    title="🌿 Roletta Greenhouse Monitor",
    sidebar=[controls],
    main=[indicators, plot_pane],
    accent_base_color="#2e7d32",
    header_background="#2e7d32"
)

# ────────────────────────────────────────────────
# Start MQTT in background + serve dashboard
# ────────────────────────────────────────────────
init_db()
mq.connect(MQTT_HOST, MQTT_PORT)
threading.Thread(target=mq.loop_forever, daemon=True).start()
update_plot()  # draw plot from any existing DB data on startup

dashboard.servable()