// riskScore.js
//
// Derived heat-stress / fatigue risk score.
// This intentionally does NOT claim to measure hydration directly (no reliable
// low-cost sensor exists for that) — instead it combines pulse, ambient heat
// index, and motion into a single 0-100 score, similar in spirit
// to occupational heat-stress guidance (e.g. WBGT-based heat index charts).
//
// Inputs expected (all numeric):
//   pulse        - beats per minute
//   ambient_temp - degrees C
//   humidity     - relative humidity %
//   motion       - 0-100 activity level (0 = still, 100 = very active)
//   mq135        - raw ADC from MQ135 (air quality / harmful gases)
//   mq5          - raw ADC from MQ5 (LPG/methane/smoke proxy)

const GAS_THRESHOLDS = {
  mq135: {
    warning: 1600,
    danger: 2200,
  },
  mq5: {
    warning: 1200,
    danger: 1800,
  },
};

function computeHeatIndex(ambientTemp, humidity) {
  // Simplified heat-index approximation (not the full NOAA regression) —
  // good enough for a relative risk signal in a hackathon demo.
  return ambientTemp + 0.05 * humidity;
}

function computeRisk({ pulse, ambient_temp, humidity, motion, mq135, mq5 }) {
  let score = 0;
  const reasons = [];
  let gasAlert = false;
  let gasSeverity = "none";

  if (pulse >= 160) { score += 40; reasons.push("very high pulse"); }
  else if (pulse >= 140) { score += 25; reasons.push("elevated pulse"); }
  else if (pulse >= 120) { score += 12; reasons.push("mildly elevated pulse"); }

  const heatIndex = computeHeatIndex(ambient_temp, humidity);
  if (heatIndex >= 40) { score += 20; reasons.push("dangerous ambient heat index"); }
  else if (heatIndex >= 35) { score += 10; reasons.push("high ambient heat index"); }

  if (motion < 20 && pulse >= 120) { score += 10; reasons.push("low motion with elevated pulse (fatigue signal)"); }

  if (Number.isFinite(mq135)) {
    if (mq135 >= GAS_THRESHOLDS.mq135.danger) {
      score += 45;
      gasAlert = true;
      gasSeverity = "danger";
      reasons.push("dangerous MQ135 gas level");
    } else if (mq135 >= GAS_THRESHOLDS.mq135.warning) {
      score += 18;
      gasAlert = true;
      if (gasSeverity !== "danger") gasSeverity = "warning";
      reasons.push("elevated MQ135 gas level");
    }
  }

  if (Number.isFinite(mq5)) {
    if (mq5 >= GAS_THRESHOLDS.mq5.danger) {
      score += 45;
      gasAlert = true;
      gasSeverity = "danger";
      reasons.push("dangerous MQ5 gas level");
    } else if (mq5 >= GAS_THRESHOLDS.mq5.warning) {
      score += 18;
      gasAlert = true;
      if (gasSeverity !== "danger") gasSeverity = "warning";
      reasons.push("elevated MQ5 gas level");
    }
  }

  score = Math.min(100, Math.round(score));

  let level = "green";
  if (score >= 60) level = "red";
  else if (score >= 30) level = "yellow";

  // Gas danger always forces red even if other vitals look normal.
  if (gasSeverity === "danger") {
    level = "red";
  }

  return {
    score,
    level,
    heatIndex: Math.round(heatIndex * 10) / 10,
    reasons,
    gasAlert,
    gasSeverity,
    thresholds: GAS_THRESHOLDS,
  };
}

function mitigationSuggestion(level, { heatIndex, reasons, gasAlert, gasSeverity }) {
  const gasIsDanger = gasSeverity === "danger" || reasons.some(r => r.includes("dangerous MQ135") || r.includes("dangerous MQ5"));
  const gasIsWarning = gasAlert && !gasIsDanger;

  if (level === "red") {
    if (gasIsDanger) {
      return "Gas hazard detected. Evacuate worker from area, provide fresh air, use respiratory protection, and trigger site safety protocol immediately.";
    }
    if (reasons.includes("dangerous ambient heat index") || reasons.includes("high ambient heat index")) {
      return "Pull worker to shade/cooling area immediately, hydrate, and pause exertion. Flag for medical check-up.";
    }
    return "Stop work, hydrate, rest in a cool area, and get a medical check-up before resuming.";
  }

  if (level === "yellow") {
    if (gasIsWarning) {
      return "Air quality is elevated — increase ventilation, move worker to fresher air if possible, and monitor gas levels closely.";
    }
    return "Monitor closely, encourage a hydration break, and consider rotating to lighter-exertion work.";
  }

  // level === "green": never claim "normal" if a gas warning is present.
  if (gasIsWarning) {
    return "Air quality is slightly elevated — increase ventilation and keep an eye on gas readings.";
  }

  return "No action needed — conditions normal.";
}

module.exports = { computeRisk, mitigationSuggestion, computeHeatIndex };