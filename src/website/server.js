// server.js
const express = require("express");
const cors = require("cors");
const http = require("http");
const path = require("path");
const { Server } = require("socket.io");

const db = require("./db");
const { computeRisk, mitigationSuggestion } = require("./riskScore");

const app = express();
const server = http.createServer(app);
const io = new Server(server, { cors: { origin: "*" } });

app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, "public")));

const FIXED_WORKER_ID = "w1";
const FIXED_WORKER_NAME = "Worker 1";

function normalizeReading(payload) {
  const pulse = Number(payload.pulse);
  const ambientTemp = Number(payload.ambient_temp);
  const humidity = Number(payload.humidity);
  const motion = Number(payload.motion);

  if ([pulse, ambientTemp, humidity, motion].some(v => Number.isNaN(v))) {
    return { error: "numeric pulse, ambient_temp, humidity, motion are required" };
  }

  return {
    worker_id: FIXED_WORKER_ID,
    worker_name: FIXED_WORKER_NAME,
    pulse,
    ambient_temp: ambientTemp,
    humidity,
    motion,
    mq135: Number(payload.mq135),
    mq5: Number(payload.mq5),
    altitude: Number(payload.altitude),
    accel_x: Number(payload.accel_x),
    accel_y: Number(payload.accel_y),
    accel_z: Number(payload.accel_z),
    packet_seq: Number(payload.packet_seq),
    fall_detected: Number(payload.fall_detected) === 1,
    sos_alert: Number(payload.sos_alert) === 1,
    gps_lat: payload.gps_lat !== undefined ? Number(payload.gps_lat) : undefined,
    gps_lng: payload.gps_lng !== undefined ? Number(payload.gps_lng) : undefined,
  };
}

function classifyIssue(risk, fallDetected, sosAlert) {
  if (sosAlert) return "sos_alert";
  if (fallDetected) return "fall_detected";  // ===== NEW =====
  if (risk.gasSeverity === "danger") return "gas_danger";
  if (risk.gasSeverity === "warning") return "gas_warning";
  if (risk.level === "red") return "risk_red";
  return null;
}

function processIncomingReading(payload, source = "api") {
  const parsed = normalizeReading(payload);
  if (parsed.error) {
    return parsed;
  }

  const risk = computeRisk({
    pulse: parsed.pulse,
    ambient_temp: parsed.ambient_temp,
    humidity: parsed.humidity,
    motion: parsed.motion,
    mq135: parsed.mq135,
    mq5: parsed.mq5,
  });

  const suggestion = mitigationSuggestion(risk.level, risk);

  const reading = {
    worker_id: parsed.worker_id,
    pulse: parsed.pulse,
    ambient_temp: parsed.ambient_temp,
    humidity: parsed.humidity,
    motion: parsed.motion,
    risk_score: risk.score,
    risk_level: risk.level,
    heat_index: risk.heatIndex,
    suggestion,
    source,
    fall_detected: parsed.fall_detected,
    sos_alert: parsed.sos_alert,
    gps_lat: parsed.gps_lat,
    gps_lng: parsed.gps_lng,
  };

  if (!Number.isNaN(parsed.mq135)) reading.mq135 = parsed.mq135;
  if (!Number.isNaN(parsed.mq5)) reading.mq5 = parsed.mq5;
  if (!Number.isNaN(parsed.altitude)) reading.altitude = parsed.altitude;
  if (!Number.isNaN(parsed.accel_x)) reading.accel_x = parsed.accel_x;
  if (!Number.isNaN(parsed.accel_y)) reading.accel_y = parsed.accel_y;
  if (!Number.isNaN(parsed.accel_z)) reading.accel_z = parsed.accel_z;
  if (!Number.isNaN(parsed.packet_seq)) reading.packet_seq = parsed.packet_seq;
  reading.gas_alert = !!risk.gasAlert;
  reading.gas_severity = risk.gasSeverity || "none";

  const record = db.addReading(reading);
  io.emit("reading", record);

  let alert = null;
  // ===== NEW: Check for SOS and fall first, then other issues =====
  const issueKey = classifyIssue(risk, parsed.fall_detected, parsed.sos_alert);
  if (issueKey) {
    const existing = db.findOpenAlert(parsed.worker_id, issueKey);
    if (!existing) {
      let message = "";
      let alertSuggestion = suggestion;
      
      if (issueKey === "sos_alert") {
        message = "WORKER#1 SOS!! 🆘";
        alertSuggestion = "CRITICAL: Worker manually triggered SOS alarm. Dispatch help immediately!";
      } else if (issueKey === "fall_detected") {
        message = "⚠️ FALL DETECTED — Immediate medical check required!";
        alertSuggestion = "Check worker status immediately. Assess for injuries and medical needs.";
      } else {
        const isGas = risk.reasons.some(r => r.includes("MQ135") || r.includes("MQ5"));
        message = isGas
          ? `Harmful gas detected — ${risk.reasons.join(", ")}.`
          : `Needs medical check-up — ${risk.reasons.join(", ")}.`;
      }
      
      alert = db.addAlert({
        worker_id: parsed.worker_id,
        level: (issueKey === "fall_detected" || issueKey === "sos_alert") ? "critical" : risk.level,
        issue_key: issueKey,
        message,
        suggestion: alertSuggestion,
        gps_lat: parsed.gps_lat,
        gps_lng: parsed.gps_lng,
        is_fall: issueKey === "fall_detected",
        is_sos: issueKey === "sos_alert"
      });
      io.emit("alert", alert);
    }
  }

  return { record, risk, suggestion, alert };
}

function setupSerialBridge() {
  const serialPath = process.env.ESP32_SERIAL_PORT || process.env.SERIAL_PORT;
  if (!serialPath) {
    console.log("ESP32_SERIAL_PORT not set; skipping serial bridge (using HTTP/simulator ingestion only).");
    return;
  }

  let SerialPort;
  try {
    ({ SerialPort } = require("serialport"));
  } catch (error) {
    console.warn("serialport package is not installed. Run npm install to enable ESP32 serial ingestion.");
    return;
  }

  const baudRate = parseInt(process.env.ESP32_SERIAL_BAUD || "115200", 10);
  const port = new SerialPort({ path: serialPath, baudRate });
  let pending = "";

  console.log(`ESP32 serial bridge listening on ${serialPath} @ ${baudRate}`);

  port.on("data", chunk => {
    pending += chunk.toString("utf8");

    let newlineIndex = pending.indexOf("\n");
    while (newlineIndex !== -1) {
      const line = pending.slice(0, newlineIndex).trim();
      pending = pending.slice(newlineIndex + 1);

      if (line.startsWith("{")) {
        try {
          const payload = JSON.parse(line);
          const result = processIncomingReading(payload, "espnow-serial");
          if (result.error) {
            console.warn("Dropped serial packet:", result.error);
          }
        } catch (error) {
          console.warn("Invalid serial JSON line from receiver:", line);
        }
      }

      newlineIndex = pending.indexOf("\n");
    }
  });

  port.on("error", error => {
    console.error("ESP32 serial bridge error:", error.message);
  });
}

// ---- REST API ----

// List workers with their latest reading attached
app.get("/api/workers", (req, res) => {
  const workers = db.listWorkers().map(w => {
    const latest = db.latestReadingFor(w.id);
    return { ...w, latest: latest || null };
  });
  res.json(workers);
});

app.post("/api/workers", (req, res) => {
  return res.status(403).json({ error: "Worker management is locked. This deployment uses Worker 1 only." });
});

app.delete("/api/workers/:id", (req, res) => {
  if (req.params.id === FIXED_WORKER_ID) {
    return res.status(403).json({ error: "Worker 1 cannot be deleted in single-worker mode." });
  }
  const existed = db.deleteWorker(req.params.id);
  if (!existed) return res.status(404).json({ error: "not found" });
  io.emit("worker_deleted", { id: req.params.id });
  res.json({ ok: true });
});

app.get("/api/workers/:id/history", (req, res) => {
  const limit = parseInt(req.query.limit) || 50;
  res.json(db.historyFor(req.params.id, limit));
});

app.get("/api/alerts", (req, res) => {
  res.json(db.listAlerts());
});

app.post("/api/alerts/:id/resolve", (req, res) => {
  const alert = db.resolveAlert(parseInt(req.params.id));
  if (!alert) return res.status(404).json({ error: "not found" });
  io.emit("alert_resolved", alert);
  res.json(alert);
});

// Let a supervisor attach a follow-up note to an alert
// (e.g. "checked on-site, given water and rest, resuming light duty")
app.post("/api/alerts/:id/note", (req, res) => {
  const { note } = req.body;
  const alert = db.setAlertNote(parseInt(req.params.id), note || "");
  if (!alert) return res.status(404).json({ error: "not found" });
  io.emit("alert_note", alert);
  res.json(alert);
});

// Main ingestion endpoint. This is what a real ESP32 (or the simulator)
// POSTs to, e.g.:
//   POST /api/readings
//   { "pulse": 132, "ambient_temp": 34, "humidity": 55, "motion": 40 }
app.post("/api/readings", (req, res) => {
  const result = processIncomingReading(req.body, "api");
  if (result.error) {
    return res.status(400).json({ error: result.error });
  }
  res.json({ ok: true, risk: result.risk, suggestion: result.suggestion });
});

const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
  console.log(`Helmet portal running at http://localhost:${PORT}`);
  setupSerialBridge();
});
