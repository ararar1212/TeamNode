// server.js
const express = require("express");
const cors = require("cors");
const http = require("http");
const { Server } = require("socket.io");

const db = require("./db");
const { computeRisk, mitigationSuggestion } = require("./riskScore");

const app = express();
const server = http.createServer(app);
const io = new Server(server, { cors: { origin: "*" } });

app.use(cors());
app.use(express.json());
app.use(express.static("public"));

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
  const { name } = req.body;
  if (!name) return res.status(400).json({ error: "name is required" });
  const worker = db.addWorker(name);
  io.emit("worker_added", worker);
  res.json(worker);
});

app.delete("/api/workers/:id", (req, res) => {
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
//   { "worker_id": "w1", "pulse": 132, "skin_temp": 37.8,
//     "ambient_temp": 34, "humidity": 55, "motion": 40 }
app.post("/api/readings", (req, res) => {
  const { worker_id, worker_name, pulse, skin_temp, ambient_temp, humidity, motion } = req.body;

  if (!worker_id || [pulse, skin_temp, ambient_temp, humidity, motion].some(v => typeof v !== "number")) {
    return res.status(400).json({
      error: "worker_id (string) and numeric pulse, skin_temp, ambient_temp, humidity, motion are required",
    });
  }

  db.ensureWorker(worker_id, worker_name);

  const risk = computeRisk({ pulse, skin_temp, ambient_temp, humidity, motion });
  const suggestion = mitigationSuggestion(risk.level, risk);

  const record = db.addReading({
    worker_id,
    pulse,
    skin_temp,
    ambient_temp,
    humidity,
    motion,
    risk_score: risk.score,
    risk_level: risk.level,
    heat_index: risk.heatIndex,
    suggestion,
  });

  io.emit("reading", record);

  if (risk.level === "red") {
    const alert = db.addAlert({
      worker_id,
      level: risk.level,
      message: `Needs medical check-up — ${risk.reasons.join(", ")}.`,
      suggestion,
    });
    io.emit("alert", alert);
  }

  res.json({ ok: true, risk, suggestion });
});

const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
  console.log(`Helmet portal running at http://localhost:${PORT}`);
});
