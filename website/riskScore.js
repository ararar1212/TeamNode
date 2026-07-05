// riskScore.js
//
// Derived heat-stress / fatigue risk score.
// This intentionally does NOT claim to measure hydration directly (no reliable
// low-cost sensor exists for that) — instead it combines pulse, skin temp,
// ambient heat index, and motion into a single 0-100 score, similar in spirit
// to occupational heat-stress guidance (e.g. WBGT-based heat index charts).
//
// Inputs expected (all numeric):
//   pulse        - beats per minute
//   skin_temp    - degrees C
//   ambient_temp - degrees C
//   humidity     - relative humidity %
//   motion       - 0-100 activity level (0 = still, 100 = very active)

function computeHeatIndex(ambientTemp, humidity) {
  // Simplified heat-index approximation (not the full NOAA regression) —
  // good enough for a relative risk signal in a hackathon demo.
  return ambientTemp + 0.05 * humidity;
}

function computeRisk({ pulse, skin_temp, ambient_temp, humidity, motion }) {
  let score = 0;
  const reasons = [];

  if (pulse >= 160) { score += 40; reasons.push("very high pulse"); }
  else if (pulse >= 140) { score += 25; reasons.push("elevated pulse"); }
  else if (pulse >= 120) { score += 12; reasons.push("mildly elevated pulse"); }

  if (skin_temp >= 38.5) { score += 30; reasons.push("high skin temperature"); }
  else if (skin_temp >= 37.5) { score += 15; reasons.push("elevated skin temperature"); }

  const heatIndex = computeHeatIndex(ambient_temp, humidity);
  if (heatIndex >= 40) { score += 20; reasons.push("dangerous ambient heat index"); }
  else if (heatIndex >= 35) { score += 10; reasons.push("high ambient heat index"); }

  if (motion < 20 && pulse >= 120) { score += 10; reasons.push("low motion with elevated pulse (fatigue signal)"); }

  score = Math.min(100, Math.round(score));

  let level = "green";
  if (score >= 60) level = "red";
  else if (score >= 30) level = "yellow";

  return { score, level, heatIndex: Math.round(heatIndex * 10) / 10, reasons };
}

function mitigationSuggestion(level, { heatIndex, reasons }) {
  if (level === "red") {
    if (reasons.includes("dangerous ambient heat index") || reasons.includes("high ambient heat index")) {
      return "Pull worker to shade/cooling area immediately, hydrate, and pause exertion. Flag for medical check-up.";
    }
    return "Stop work, hydrate, rest in a cool area, and get a medical check-up before resuming.";
  }
  if (level === "yellow") {
    return "Monitor closely, encourage a hydration break, and consider rotating to lighter-exertion work.";
  }
  return "No action needed — conditions normal.";
}

module.exports = { computeRisk, mitigationSuggestion, computeHeatIndex };
