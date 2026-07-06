// helmet3d.js
// Builds a rotating 3D helmet with glowing sensor markers driven by live
// worker data. Exposed on window.Helmet3D so the non-module dashboard.js
// can call it directly.

import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { CSS2DRenderer, CSS2DObject } from "three/addons/renderers/CSS2DRenderer.js";

const RISK_COLORS = {
  green: 0x3ddc97,
  yellow: 0xf2c94c,
  red: 0xeb5757,
  none: 0x8b93a7,
};

let scene, camera, renderer, labelRenderer, controls, container;
let helmetGroup;
let ledMesh, ledLight;
let markers = {}; // name -> { mesh, label }
let alertPulse = false;
let clock = new THREE.Clock();

function makeLabel(className, text) {
  const div = document.createElement("div");
  div.className = "sensor-label " + className;
  div.textContent = text;
  return new CSS2DObject(div);
}

function buildHelmet() {
  helmetGroup = new THREE.Group();

  // Main shell — a dome cap
  const shellGeo = new THREE.SphereGeometry(1, 48, 32, 0, Math.PI * 2, 0, Math.PI * 0.55);
  const shellMat = new THREE.MeshStandardMaterial({ color: 0xf2a93b, roughness: 0.45, metalness: 0.15 });
  const shell = new THREE.Mesh(shellGeo, shellMat);
  shell.scale.set(1, 0.82, 1.05);
  shell.position.y = 0.15;
  helmetGroup.add(shell);

  // Brim
  const brimGeo = new THREE.TorusGeometry(1.05, 0.07, 16, 60);
  const brimMat = new THREE.MeshStandardMaterial({ color: 0xc2790b, roughness: 0.5, metalness: 0.1 });
  const brim = new THREE.Mesh(brimGeo, brimMat);
  brim.rotation.x = Math.PI / 2;
  brim.scale.set(1, 1, 0.6);
  brim.position.y = -0.16;
  helmetGroup.add(brim);

  // Rear electronics bay
  const bayGeo = new THREE.BoxGeometry(0.34, 0.24, 0.28);
  const bayMat = new THREE.MeshStandardMaterial({ color: 0x2c3e63, roughness: 0.4, metalness: 0.4 });
  const bay = new THREE.Mesh(bayGeo, bayMat);
  bay.position.set(0, 0.08, -1.02);
  helmetGroup.add(bay);

  // Small antenna
  const antGeo = new THREE.CylinderGeometry(0.012, 0.012, 0.32, 8);
  const antMat = new THREE.MeshStandardMaterial({ color: 0xb7c1d9, metalness: 0.7, roughness: 0.3 });
  const antenna = new THREE.Mesh(antGeo, antMat);
  antenna.position.set(0.12, 0.28, -1.1);
  antenna.rotation.z = -0.3;
  helmetGroup.add(antenna);

  // ---- Sensor markers ----
  function addMarker(name, position, color, labelText) {
    const geo = new THREE.SphereGeometry(0.055, 20, 20);
    const mat = new THREE.MeshStandardMaterial({
      color, emissive: color, emissiveIntensity: 0.6, roughness: 0.3,
    });
    const mesh = new THREE.Mesh(geo, mat);
    mesh.position.copy(position);
    helmetGroup.add(mesh);

    const label = makeLabel("label-" + name, labelText);
    label.position.set(position.x, position.y + 0.12, position.z);
    helmetGroup.add(label);

    markers[name] = { mesh, label, div: label.element };
  }

  addMarker("ambient", new THREE.Vector3(0.4, 0.78, -0.55), 0x86efac, "Ambient: —");
  addMarker("pulse", new THREE.Vector3(-0.85, 0.15, 0.35), 0xfca5a5, "Pulse: —");
  addMarker("gas", new THREE.Vector3(0, 0.35, 0.98), 0xfdba74, "Gas: —");
  addMarker("motion", new THREE.Vector3(0, 0.98, 0.05), 0xc4b5fd, "Motion: —");

  // LED status marker — driven by risk level
  const ledGeo = new THREE.SphereGeometry(0.07, 20, 20);
  const ledMat = new THREE.MeshStandardMaterial({
    color: RISK_COLORS.none, emissive: RISK_COLORS.none, emissiveIntensity: 1.2, roughness: 0.2,
  });
  ledMesh = new THREE.Mesh(ledGeo, ledMat);
  ledMesh.position.set(0, -0.08, 1.08);
  helmetGroup.add(ledMesh);

  ledLight = new THREE.PointLight(RISK_COLORS.none, 0.8, 1.5);
  ledLight.position.copy(ledMesh.position);
  helmetGroup.add(ledLight);

  const ledLabel = makeLabel("label-led sensor-label-status", "Status: No data");
  ledLabel.position.set(0, 0.04, 1.08);
  helmetGroup.add(ledLabel);
  markers.led = { mesh: ledMesh, label: ledLabel, div: ledLabel.element };

  scene.add(helmetGroup);
}

function onResize() {
  if (!container) return;
  const w = container.clientWidth;
  const h = container.clientHeight;
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
  renderer.setSize(w, h);
  labelRenderer.setSize(w, h);
}

function animate() {
  requestAnimationFrame(animate);
  const dt = clock.getDelta();

  if (helmetGroup) helmetGroup.rotation.y += dt * 0.35;

  if (alertPulse && ledMesh) {
    const t = performance.now() * 0.005;
    const pulse = 1.0 + Math.sin(t) * 0.6;
    ledMesh.material.emissiveIntensity = 1.2 * pulse;
    if (ledLight) ledLight.intensity = 0.8 * pulse;
  }

  controls.update();
  renderer.render(scene, camera);
  labelRenderer.render(scene, camera);
}

function init(containerEl) {
  container = containerEl;
  const w = container.clientWidth;
  const h = container.clientHeight;

  scene = new THREE.Scene();

  camera = new THREE.PerspectiveCamera(45, w / h, 0.1, 100);
  camera.position.set(0, 1.1, 3.1);

  renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
  renderer.setPixelRatio(window.devicePixelRatio || 1);
  renderer.setSize(w, h);
  container.appendChild(renderer.domElement);

  labelRenderer = new CSS2DRenderer();
  labelRenderer.setSize(w, h);
  labelRenderer.domElement.style.position = "absolute";
  labelRenderer.domElement.style.top = "0";
  labelRenderer.domElement.style.pointerEvents = "none";
  container.appendChild(labelRenderer.domElement);

  controls = new OrbitControls(camera, labelRenderer.domElement);
  controls.target.set(0, 0.3, 0);
  controls.enableDamping = true;
  controls.dampingFactor = 0.08;
  controls.minDistance = 1.8;
  controls.maxDistance = 5;
  controls.update();

  scene.add(new THREE.AmbientLight(0xffffff, 0.6));
  const dirLight = new THREE.DirectionalLight(0xffffff, 0.8);
  dirLight.position.set(2, 4, 3);
  scene.add(dirLight);
  const fillLight = new THREE.PointLight(0xffc98a, 0.4);
  fillLight.position.set(-2, 1, -2);
  scene.add(fillLight);

  buildHelmet();
  window.addEventListener("resize", onResize);
  animate();
}

function update(reading, workerName) {
  if (!markers.pulse) return;

  markers.pulse.div.textContent = `Pulse: ${reading.pulse} bpm`;
  markers.gas.div.textContent = `Gas: ${fmtInt(reading.mq135)} / ${fmtInt(reading.mq5)}`;
  markers.ambient.div.textContent = `Ambient: ${reading.ambient_temp}°C / ${reading.humidity}%`;
  markers.motion.div.textContent = `Motion: ${reading.motion}`;

  const level = reading.risk_level || "none";
  const color = RISK_COLORS[level] ?? RISK_COLORS.none;
  ledMesh.material.color.setHex(color);
  ledMesh.material.emissive.setHex(color);
  ledLight.color.setHex(color);

  alertPulse = level === "red";
  if (!alertPulse) {
    ledMesh.material.emissiveIntensity = 1.2;
    ledLight.intensity = 0.8;
  }

  const statusText = { green: "Normal", yellow: "Monitor", red: "Needs check-up" }[level] || "No data";
  markers.led.div.textContent = `${workerName || ""}: ${statusText}`;
  markers.led.div.className = "sensor-label label-led sensor-label-status status-" + level;
}

function showNoData(workerName) {
  if (!markers.led) return;
  ["pulse", "gas", "ambient", "motion"].forEach(k => {
    markers[k].div.textContent = markers[k].div.textContent.split(":")[0] + ": —";
  });
  ledMesh.material.color.setHex(RISK_COLORS.none);
  ledMesh.material.emissive.setHex(RISK_COLORS.none);
  ledLight.color.setHex(RISK_COLORS.none);
  alertPulse = false;
  ledMesh.material.emissiveIntensity = 0.5;
  markers.led.div.textContent = `${workerName || ""}: No data`;
  markers.led.div.className = "sensor-label label-led sensor-label-status status-none";
}

function fmtInt(value) {
  return Number.isFinite(Number(value)) ? Number(value) : "-";
}

window.Helmet3D = { init, update, showNoData };
