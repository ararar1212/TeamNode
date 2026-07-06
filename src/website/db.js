// db.js
// Lightweight file-backed JSON store. Keeps everything in memory for speed,
// and persists to disk so data survives a server restart during the demo.
//
// Swap-to-AWS note: replace the read/write/query functions in this file with
// DynamoDB SDK calls (same function names/shapes) and nothing else in the
// app needs to change.

const fs = require("fs");
const path = require("path");

const DB_FILE = path.join(__dirname, "data.json");

function loadInitial() {
  if (fs.existsSync(DB_FILE)) {
    try {
      fs.unlinkSync(DB_FILE);
      console.log("Deleted old data.json to start fresh.");
    } catch (e) {
      console.warn("Could not delete data.json.");
    }
  }
  return {
    workers: [
      { id: "w1", name: "Worker 1" },
    ],
    readings: [],   // { id, worker_id, pulse, ambient_temp, humidity, motion, risk_score, risk_level, ts }
    alerts: [],     // { id, worker_id, message, level, ts, resolved }
  };
}

const state = loadInitial();
let nextReadingId = state.readings.length
  ? Math.max(...state.readings.map(r => r.id)) + 1
  : 1;
let nextAlertId = state.alerts.length
  ? Math.max(...state.alerts.map(a => a.id)) + 1
  : 1;

let saveTimer = null;
function persist() {
  // Debounce disk writes so a burst of sensor readings doesn't hammer the disk.
  if (saveTimer) return;
  saveTimer = setTimeout(() => {
    fs.writeFile(DB_FILE, JSON.stringify(state, null, 2), () => {});
    saveTimer = null;
  }, 500);
}

const MAX_READINGS_PER_WORKER = 200; // cap history kept in memory per worker

module.exports = {
  listWorkers() {
    // Keep a stable single-worker setup for hardware demos.
    if (!state.workers.length) {
      state.workers.push({ id: "w1", name: "Worker 1" });
    }
    return state.workers;
  },

  addWorker(name) {
    const id = "w" + (state.workers.length + 1) + "_" + Date.now().toString(36);
    const worker = { id, name };
    state.workers.push(worker);
    persist();
    return worker;
  },

  ensureWorker(workerId, fallbackName) {
    let w = state.workers.find(w => w.id === workerId);
    if (!w) {
      w = { id: workerId, name: fallbackName || workerId };
      state.workers.push(w);
      persist();
    }
    return w;
  },

  addReading(reading) {
    const record = { id: nextReadingId++, ts: Date.now(), ...reading };
    state.readings.push(record);

    // trim history for this worker to keep memory/disk bounded
    const forWorker = state.readings.filter(r => r.worker_id === record.worker_id);
    if (forWorker.length > MAX_READINGS_PER_WORKER) {
      const toRemove = forWorker.length - MAX_READINGS_PER_WORKER;
      let removed = 0;
      state.readings = state.readings.filter(r => {
        if (r.worker_id === record.worker_id && removed < toRemove) {
          removed++;
          return false;
        }
        return true;
      });
    }
    persist();
    return record;
  },

  latestReadingFor(workerId) {
    const forWorker = state.readings.filter(r => r.worker_id === workerId);
    return forWorker.length ? forWorker[forWorker.length - 1] : null;
  },

  historyFor(workerId, limit = 50) {
    const forWorker = state.readings.filter(r => r.worker_id === workerId);
    return forWorker.slice(-limit);
  },

  addAlert(alert) {
    const record = { id: nextAlertId++, ts: Date.now(), resolved: false, note: "", ...alert };
    state.alerts.unshift(record);
    state.alerts = state.alerts.slice(0, 200); // keep last 200
    persist();
    return record;
  },

  findOpenAlert(workerId, issueKey) {
    return state.alerts.find(a => a.worker_id === workerId && a.issue_key === issueKey && !a.resolved) || null;
  },

  listAlerts() {
    return state.alerts;
  },

  resolveAlert(alertId) {
    const a = state.alerts.find(a => a.id === alertId);
    if (a) {
      a.resolved = true;
      persist();
    }
    return a;
  },

  setAlertNote(alertId, note) {
    const a = state.alerts.find(a => a.id === alertId);
    if (a) {
      a.note = note;
      persist();
    }
    return a;
  },

  deleteWorker(workerId) {
    const existed = state.workers.some(w => w.id === workerId);
    state.workers = state.workers.filter(w => w.id !== workerId);
    state.readings = state.readings.filter(r => r.worker_id !== workerId);
    state.alerts = state.alerts.filter(a => a.worker_id !== workerId);
    persist();
    return existed;
  },
};
