// simulate.js
//
// Fakes 3 helmets sending sensor data over HTTP, the same way a real ESP32
// would (same endpoint, same JSON shape). Values random-walk over time and
// occasionally spike into "red" territory so you can demo the alert flow
// without hardware.
//
// Run alongside the server:
//   node simulate.js
//
// To point a real ESP32 here instead, just POST the same JSON shape to
// http://<server-ip>:3000/api/readings from the device's firmware.

const SERVER_URL = process.env.SERVER_URL || "http://localhost:3000";

const workers = [
  { id: "w1", name: "Worker 1" },
  { id: "w2", name: "Worker 2" },
  { id: "w3", name: "Worker 3" },
  { id: "w4", name: "Worker 4 (at risk)", alwaysAtRisk: true },
];

// running state per worker, so values drift realistically instead of jumping randomly
const state = {};
workers.forEach(w => {
  if (w.alwaysAtRisk) {
    // Starts already in heat-stress territory and stays there, drifting only
    // slightly, so the demo always has a visible red-alert worker right away
    // without waiting for a random spike.
    state[w.id] = {
      pulse: 150,
      skin_temp: 38.6,
      ambient_temp: 37,
      humidity: 65,
      motion: 12,
      spikeUntil: Infinity,
    };
  } else {
    state[w.id] = {
      pulse: 80 + Math.random() * 10,
      skin_temp: 36.5 + Math.random() * 0.5,
      ambient_temp: 30 + Math.random() * 3,
      humidity: 50 + Math.random() * 10,
      motion: 40 + Math.random() * 20,
      spikeUntil: 0, // timestamp; while in the future, push this worker toward "red"
    };
  }
});

function clamp(v, min, max) { return Math.max(min, Math.min(max, v)); }
function drift(value, amount, min, max) {
  return clamp(value + (Math.random() - 0.5) * amount, min, max);
}

function tick(worker) {
  const s = state[worker.id];
  const now = Date.now();

  // Randomly start a "spike" occasionally (simulate a worker overheating/overexerting)
  if (now > s.spikeUntil && Math.random() < 0.03) {
    s.spikeUntil = now + (15000 + Math.random() * 15000); // 15-30s spike window
    console.log(`⚠️  Simulating a heat-stress spike for ${worker.name}`);
  }

  const spiking = now < s.spikeUntil;

  if (spiking) {
    s.pulse = drift(s.pulse + 2, 6, 100, 185);
    s.skin_temp = drift(s.skin_temp + 0.1, 0.3, 36, 39.5);
    s.ambient_temp = drift(s.ambient_temp + 0.2, 1, 28, 42);
    s.motion = drift(s.motion - 2, 8, 0, 100);
  } else {
    s.pulse = drift(s.pulse, 4, 65, 110);
    s.skin_temp = drift(s.skin_temp, 0.15, 36.2, 37.4);
    s.ambient_temp = drift(s.ambient_temp, 0.8, 26, 34);
    s.motion = drift(s.motion, 10, 20, 90);
  }
  s.humidity = drift(s.humidity, 3, 40, 80);

  return {
    worker_id: worker.id,
    worker_name: worker.name,
    pulse: Math.round(s.pulse),
    skin_temp: Math.round(s.skin_temp * 10) / 10,
    ambient_temp: Math.round(s.ambient_temp * 10) / 10,
    humidity: Math.round(s.humidity),
    motion: Math.round(s.motion),
  };
}

async function sendReading(payload) {
  try {
    const res = await fetch(`${SERVER_URL}/api/readings`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    const data = await res.json();
    if (data.risk?.level === "red") {
      console.log(`🔴 ${payload.worker_name}: RED — ${data.suggestion}`);
    }
  } catch (err) {
    console.error("Failed to send reading:", err.message);
  }
}

console.log(`Simulating ${workers.length} helmets → ${SERVER_URL}/api/readings`);
console.log("Press Ctrl+C to stop.\n");

setInterval(() => {
  workers.forEach(w => sendReading(tick(w)));
}, 3000);
