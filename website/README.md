# Smart Safety Helmet — Web Portal

A live dashboard that receives sensor readings from a helmet (real ESP32 or
the included simulator), computes a heat-stress/fatigue risk score per
worker, logs everything, and flags workers who need a medical check-up.

## What's inside

- `server.js` — Express + Socket.io backend. Exposes the ingestion API and
  serves the dashboard.
- `riskScore.js` — the heat-stress/fatigue scoring logic (pulse, skin temp,
  ambient heat index, motion → green/yellow/red + a mitigation suggestion).
- `db.js` — simple JSON-file data store (no database setup required for the
  hackathon). Structured so it's a near drop-in swap for DynamoDB later.
- `simulate.js` — fakes 4 helmets sending realistic sensor data over HTTP:
  3 normal workers with occasional random "spikes", plus one worker
  (`Worker 4 (at risk)`) that starts already in heat-stress territory and
  stays there, so a red alert is visible immediately without waiting.
- `public/` — the dashboard frontend (vanilla JS + Chart.js, no build step)
  plus a Three.js-powered 3D helmet view (`helmet3d.js`) that loads Three.js
  straight from a CDN — no local install needed.

## Running it

```bash
npm install
node server.js
```

Then open **http://localhost:3000** in your browser.

In a second terminal, start the simulator to see live data flow in:

```bash
node simulate.js
```

You'll see worker cards update in real time. Click a card to see a history
chart plus a readable data table of the same readings. A red alert appears
in the sidebar whenever a worker crosses the risk threshold — each alert
shows the suggested mitigation action, and you (as supervisor) can attach
your own follow-up note (e.g. "checked on-site, given water, resuming light
duty") or mark it resolved. You can also delete a worker (from their card or
their detail view) — this removes their history and alerts too.

At the top of the page, a rotating 3D helmet shows live sensor readings as
glowing markers with floating labels (pulse, skin temp, ambient, motion),
plus a status light that turns green/yellow/red and pulses when a worker
needs a check-up. Use the dropdown to lock it to a specific worker, or leave
it on "Auto" to always show whoever's currently most at-risk. Drag to orbit
the camera, scroll to zoom — it also spins on its own.

Requires an internet connection the first time it loads (it pulls Three.js
from a CDN); everything else runs fully offline/local.

## Connecting a real ESP32

Point your ESP32's HTTP client at:

```
POST http://<your-computer's-IP>:3000/api/readings
Content-Type: application/json

{
  "worker_id": "w1",
  "worker_name": "Worker 1",
  "pulse": 132,
  "skin_temp": 37.8,
  "ambient_temp": 34,
  "humidity": 55,
  "motion": 40
}
```

- `pulse`: beats per minute (from MAX30102)
- `skin_temp`: °C (from MLX90614)
- `ambient_temp` / `humidity`: from SHT31
- `motion`: 0–100 activity level derived from the MPU6050 (e.g. accelerometer
  variance mapped to a 0–100 scale)

The server computes the risk score and stores/broadcasts everything — the
ESP32 only needs to send raw readings, no scoring logic required on-device
(though you can mirror the same logic on the ESP32 to drive the local LED
even when offline, as described in the hardware report).

Make sure your laptop and the ESP32 are on the same Wi-Fi network, and use
your laptop's local IP (not `localhost`) in the ESP32 firmware.

## Moving to AWS later (optional, for bonus points)

This is built so the swap is small, not a rewrite:

- **Data store**: replace the functions in `db.js` with equivalent DynamoDB
  SDK calls — the rest of the app calls `db.addReading()`, `db.listWorkers()`
  etc. and doesn't care how they're implemented.
- **Hosting**: deploy `server.js` as-is to an EC2 instance or Elastic
  Beanstalk, or wrap it for AWS Lambda + API Gateway if you want serverless.
- **Frontend**: `public/` is static and can be hosted on AWS Amplify or
  S3 + CloudFront directly, pointed at your deployed API.
- **IoT bonus**: you could route ESP32 → AWS IoT Core → a small Lambda that
  forwards to `/api/readings`, if you want to show AWS IoT Core in the demo.

## Notes on the risk score

There's no reliable low-cost sensor for direct hydration measurement, so the
score is a **derived heat-stress/fatigue indicator** combining pulse, skin
temperature, ambient heat index, and motion — conceptually similar to
occupational heat-stress guidance (e.g. WBGT-based heat index charts). It's
intentionally framed as a relative risk signal, not a medical diagnosis.
