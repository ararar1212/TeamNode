const socket = io();

const workerGrid = document.getElementById("workerGrid");
const alertList = document.getElementById("alertList");
const connStatus = document.getElementById("connStatus");
const addWorkerBtn = document.getElementById("addWorkerBtn");
const modal = document.getElementById("detailModal");
const closeModal = document.getElementById("closeModal");
const modalTitle = document.getElementById("modalTitle");
const modalReadings = document.getElementById("modalReadings");

let workers = {};   // id -> worker object (with .latest)
let alerts = [];
let activeWorkerId = null;

// ---- 3D helmet view ----
const helmet3dSelect = document.getElementById("helmet3dSelect");
const helmet3dContainer = document.getElementById("helmet3dContainer");
let helmet3dMode = "auto"; // "auto" or a worker id
let helmet3dReady = false;

function initHelmet3D() {
  if (helmet3dReady) return;
  if (window.Helmet3D && helmet3dContainer) {
    window.Helmet3D.init(helmet3dContainer);
    helmet3dReady = true;
    refreshHelmet3D();
  } else {
    // Three.js module may still be loading over the network — retry briefly.
    setTimeout(initHelmet3D, 150);
  }
}

function pickAutoWorker() {
  const withData = Object.values(workers).filter(w => w.latest);
  if (!withData.length) return null;
  const order = { red: 2, yellow: 1, green: 0 };
  withData.sort((a, b) => (order[b.latest.risk_level] ?? -1) - (order[a.latest.risk_level] ?? -1));
  return withData[0];
}

function refreshHelmet3D() {
  if (!helmet3dReady) return;
  const targetId = helmet3dMode === "auto" ? pickAutoWorker()?.id : helmet3dMode;
  const worker = targetId ? workers[targetId] : null;
  if (worker && worker.latest) {
    window.Helmet3D.update(worker.latest, worker.name);
  } else {
    window.Helmet3D.showNoData(worker ? worker.name : "");
  }
}

function renderHelmet3DOptions() {
  const current = helmet3dSelect.value || "auto";
  helmet3dSelect.innerHTML = `<option value="auto">Auto (most at-risk)</option>` +
    Object.values(workers).map(w => `<option value="${w.id}">${w.name}</option>`).join("");
  // keep selection if the worker still exists, otherwise fall back to auto
  helmet3dSelect.value = workers[current] ? current : "auto";
  helmet3dMode = helmet3dSelect.value;
}

helmet3dSelect.onchange = () => {
  helmet3dMode = helmet3dSelect.value;
  refreshHelmet3D();
};

// ---- Connection status ----
socket.on("connect", () => {
  connStatus.textContent = "Live";
  connStatus.className = "status-pill status-connected";
});
socket.on("disconnect", () => {
  connStatus.textContent = "Disconnected";
  connStatus.className = "status-pill status-disconnected";
});

// ---- Initial load ----
async function loadWorkers() {
  const res = await fetch("/api/workers");
  const list = await res.json();
  workers = {};
  list.forEach(w => (workers[w.id] = w));
  renderWorkers();
  renderHelmet3DOptions();
  initHelmet3D();
  refreshHelmet3D();
}

async function loadAlerts() {
  const res = await fetch("/api/alerts");
  alerts = await res.json();
  renderAlerts();
}

// ---- Rendering ----
function riskLabel(level) {
  return { green: "Normal", yellow: "Monitor", red: "Needs check-up", none: "No data" }[level || "none"];
}

function fmtInt(value) {
  return Number.isFinite(Number(value)) ? Number(value) : "-";
}

function gasStatus(latest) {
  if (!latest) return "none";
  if (latest.gas_severity === "danger") return "danger";
  if (latest.gas_severity === "warning") return "warning";
  return "none";
}

function renderWorkers() {
  workerGrid.innerHTML = "";
  Object.values(workers).forEach(w => {
    const latest = w.latest;
    const level = latest ? latest.risk_level : "none";
    const gas = gasStatus(latest);
    const card = document.createElement("div");
    card.className = `worker-card level-${level}`;
    card.onclick = () => openDetail(w.id);

    card.innerHTML = `
      <div class="worker-card-top">
        <div class="worker-name-wrap">
          <span class="worker-name">${w.name}</span>
        </div>
        <div class="worker-name-wrap">
          <span class="risk-badge level-${level}">${riskLabel(level)}</span>
          ${gas === "none" ? "" : `<span class="gas-badge ${gas}">Gas ${gas === "danger" ? "Danger" : "Warning"}</span>`}
          <button class="card-delete" title="Delete worker" data-id="${w.id}">✕</button>
        </div>
      </div>
      ${latest ? `
        <div class="metric-row"><span>Pulse</span><b>${latest.pulse} bpm</b></div>
        <div class="metric-row"><span>Ambient</span><b>${latest.ambient_temp}°C / ${latest.humidity}%</b></div>
        <div class="metric-row"><span>Motion</span><b>${latest.motion}</b></div>
        <div class="metric-row"><span>MQ135 / MQ5</span><b>${fmtInt(latest.mq135)} / ${fmtInt(latest.mq5)}</b></div>
        <div class="suggestion">${latest.suggestion}</div>
      ` : `<div class="no-data">Waiting for sensor data…</div>`}
    `;
    card.querySelector(".card-delete").onclick = (e) => {
      e.stopPropagation();
      deleteWorker(w.id, w.name);
    };
    workerGrid.appendChild(card);
  });
}

function renderAlerts() {
  if (!alerts.length) {
    alertList.innerHTML = `<div class="empty">No alerts yet.</div>`;
    return;
  }
  alertList.innerHTML = "";
  alerts.forEach(a => {
    const w = workers[a.worker_id];
    const div = document.createElement("div");
    div.className = "alert-item" + (a.resolved ? " resolved" : "");
    div.innerHTML = `
      <div class="alert-time">${new Date(a.ts).toLocaleTimeString()} — ${w ? w.name : a.worker_id}</div>
      <div>${a.message}</div>
      <div class="alert-suggestion"><b>Suggested action:</b> ${a.suggestion || ""}</div>
      ${!a.resolved ? `<button class="resolve-btn" data-id="${a.id}">Mark resolved</button>` : ""}
      ${a.note ? `<div class="saved-note">📝 ${a.note}</div>` : ""}
      <div class="alert-note-row">
        <input type="text" placeholder="Add a follow-up note…" data-note-id="${a.id}" />
        <button data-save-note="${a.id}">Save</button>
      </div>
    `;
    alertList.appendChild(div);
  });
  alertList.querySelectorAll(".resolve-btn").forEach(btn => {
    btn.onclick = async () => {
      await fetch(`/api/alerts/${btn.dataset.id}/resolve`, { method: "POST" });
    };
  });
  alertList.querySelectorAll("[data-save-note]").forEach(btn => {
    btn.onclick = async () => {
      const id = btn.dataset.saveNote;
      const input = alertList.querySelector(`input[data-note-id="${id}"]`);
      const note = input.value.trim();
      if (!note) return;
      await fetch(`/api/alerts/${id}/note`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ note }),
      });
    };
  });
}

// ---- Detail modal + history chart ----
async function openDetail(workerId) {
  activeWorkerId = workerId;
  const w = workers[workerId];
  modalTitle.textContent = w.name;
  modal.classList.remove("hidden");
  renderModalReadings();
  const res = await fetch(`/api/workers/${workerId}/history?limit=40`);
  const history = await res.json();
  renderHistoryTable(history);
}

function renderHistoryTable(history) {
  const tbody = document.getElementById("historyTableBody");
  if (!history.length) {
    tbody.innerHTML = `<tr><td colspan="8" class="empty">No data yet</td></tr>`;
    return;
  }
  // most recent first, easier to read at a glance
  tbody.innerHTML = [...history].reverse().map(h => `
    <tr>
      <td>${new Date(h.ts).toLocaleTimeString()}</td>
      <td>${h.pulse}</td>
      <td>${h.ambient_temp}</td>
      <td>${h.humidity}</td>
      <td>${h.motion}</td>
      <td>${fmtInt(h.mq135)}</td>
      <td>${fmtInt(h.mq5)}</td>
      <td class="risk-${h.risk_level}">${h.risk_level.toUpperCase()} (${h.risk_score})</td>
    </tr>
  `).join("");
}

async function deleteWorker(id, name) {
  if (!confirm(`Delete ${name}? This removes their history and alerts too.`)) return;
  await fetch(`/api/workers/${id}`, { method: "DELETE" });
}

function renderModalReadings() {
  const w = workers[activeWorkerId];
  const latest = w?.latest;
  modalReadings.innerHTML = latest ? `
    <div>Pulse<b>${latest.pulse} bpm</b></div>
    <div>Ambient<b>${latest.ambient_temp}°C</b></div>
    <div>Humidity<b>${latest.humidity}%</b></div>
    <div>Motion<b>${latest.motion}</b></div>
    <div>MQ135<b>${fmtInt(latest.mq135)}</b></div>
    <div>MQ5<b>${fmtInt(latest.mq5)}</b></div>
    <div>Risk score<b>${latest.risk_score}/100</b></div>
  ` : `<div class="no-data">No data yet</div>`;
}


closeModal.onclick = () => modal.classList.add("hidden");
modal.onclick = (e) => { if (e.target === modal) modal.classList.add("hidden"); };

document.getElementById("deleteWorkerBtn").onclick = () => {
  if (!activeWorkerId) return;
  const w = workers[activeWorkerId];
  deleteWorker(activeWorkerId, w ? w.name : activeWorkerId);
  modal.classList.add("hidden");
};

// ---- Add worker ----
addWorkerBtn.onclick = async () => {
  const name = prompt("New worker name:");
  if (!name) return;
  await fetch("/api/workers", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ name }),
  });
};

// ---- Live updates via socket ----
socket.on("reading", (record) => {
  if (!workers[record.worker_id]) {
    workers[record.worker_id] = { id: record.worker_id, name: record.worker_id, latest: record };
  } else {
    workers[record.worker_id].latest = record;
  }
  renderWorkers();
  refreshHelmet3D();
  if (activeWorkerId === record.worker_id) {
    renderModalReadings();
    fetch(`/api/workers/${activeWorkerId}/history?limit=40`)
      .then(r => r.json())
  }
});

socket.on("worker_deleted", ({ id }) => {
  delete workers[id];
  renderWorkers();
  renderHelmet3DOptions();
  refreshHelmet3D();
  if (activeWorkerId === id) {
    modal.classList.add("hidden");
    activeWorkerId = null;
  }
});

socket.on("alert_note", (alert) => {
  const idx = alerts.findIndex(a => a.id === alert.id);
  if (idx !== -1) alerts[idx] = alert;
  renderAlerts();
});

socket.on("alert", (alert) => {
  alerts.unshift(alert);
  renderAlerts();
});

socket.on("alert_resolved", (alert) => {
  const idx = alerts.findIndex(a => a.id === alert.id);
  if (idx !== -1) alerts[idx] = alert;
  renderAlerts();
});

socket.on("worker_added", (worker) => {
  workers[worker.id] = worker;
  renderWorkers();
  renderHelmet3DOptions();
});

// ---- Init ----
loadWorkers();
loadAlerts();
