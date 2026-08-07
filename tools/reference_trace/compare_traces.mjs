// R1.3B Phase 0B Step 9: cross-implementation trace comparison.
//
// Aligns a WISTERIA canonical trace (JSONL) with a babylon reference
// per-body trace (bodies.csv) by motionFrame and sourceRigidBodyIndex and
// reports first/max divergence. The reference CSV is already normalized to
// the WISTERIA canonical coordinate (ReferenceCoordinateNormalization v1).
//
// usage:
//   node compare_traces.mjs --wisteria <trace.jsonl> --reference <bodies.csv>
//       [--env <env.json>]

import { readFileSync } from "node:fs";

const EPSILON = 1e-6;

const args = process.argv.slice(2);
const take = (name) => {
  const index = args.indexOf(name);
  if (index < 0 || index + 1 >= args.length) {
    console.error(`missing ${name}`);
    process.exit(2);
  }
  return args[index + 1];
};
const wisteriaPath = take("--wisteria");
const referencePath = take("--reference");
const envPath = args.includes("--env")
  ? args[args.indexOf("--env") + 1]
  : null;

const rotationErrorDeg = (a, b) => {
  if (a.every((v, i) => v === b[i])) return 0;
  const column = (m, c) => [m[c * 3], m[c * 3 + 1], m[c * 3 + 2]];
  const length = (v) => Math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  const dot = (x, y) => x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
  let trace = 0;
  for (let c = 0; c < 3; ++c) {
    const ca = column(a, c);
    const cb = column(b, c);
    const la = length(ca);
    const lb = length(cb);
    if (la <= 1e-12 || lb <= 1e-12) return 0;
    trace += dot(
      [ca[0] / la, ca[1] / la, ca[2] / la],
      [cb[0] / lb, cb[1] / lb, cb[2] / lb]
    );
  }
  const clamped = Math.max(-1, Math.min(1, (trace - 1) * 0.5));
  return (Math.acos(clamped) * 180) / Math.PI;
};

const readWisteria = (path) => {
  const frames = new Map();
  for (const line of readFileSync(path, "utf8").split("\n")) {
    if (!line.trim()) continue;
    const frame = JSON.parse(line);
    const bodies = new Map();
    for (const body of frame.bodies) {
      bodies.set(body.index, {
        position: body.worldTransform.position,
        rotation: body.worldTransform.rotationBasis
      });
    }
    frames.set(frame.frame, { bodies, canonical: frame.canonical });
  }
  return frames;
};

const readReference = (path) => {
  const lines = readFileSync(path, "utf8").split("\n");
  const header = lines[0].trim().split(",");
  const column = (name) => header.indexOf(name);
  const col = Object.fromEntries(
    header.map((name, index) => [name, index])
  );
  const frames = new Map();
  for (let lineIndex = 1; lineIndex < lines.length; ++lineIndex) {
    const line = lines[lineIndex].trim();
    if (!line) continue;
    const fields = line.split(",");
    const motionFrame = Number(fields[col.motionFrame]);
    const bodyIndex = Number(fields[col.sourceRigidBodyIndex]);
    let frame = frames.get(motionFrame);
    if (!frame) {
      frame = { bodies: new Map() };
      frames.set(motionFrame, frame);
    }
    const rotation = [];
    for (const name of [
      "rot00", "rot01", "rot02",
      "rot10", "rot11", "rot12",
      "rot20", "rot21", "rot22"
    ]) {
      rotation.push(Number(fields[col[name]]));
    }
    frame.bodies.set(bodyIndex, {
      position: [
        Number(fields[col.posX]),
        Number(fields[col.posY]),
        Number(fields[col.posZ])
      ],
      rotation
    });
  }
  return frames;
};

const wisteria = readWisteria(wisteriaPath);
const reference = readReference(referencePath);

let first = null;
let max = null;
let comparedFrames = 0;
let comparedBodies = 0;
let missingBodies = 0;

const update = (frame, bodyIndex, positionError, rotationErrorDeg, missing) => {
  if (!first && (positionError > EPSILON || rotationErrorDeg > EPSILON || missing)) {
    first = { frame, bodyIndex, positionError, rotationErrorDeg, missing };
  }
  if (!max || positionError > max.positionError) {
    max = { frame, bodyIndex, positionError, rotationErrorDeg, missing };
  }
};

for (const [frame, referenceFrame] of reference) {
  const wisteriaFrame = wisteria.get(frame);
  if (!wisteriaFrame) continue;
  ++comparedFrames;
  const indices = new Set([
    ...wisteriaFrame.bodies.keys(),
    ...referenceFrame.bodies.keys()
  ]);
  for (const index of indices) {
    const left = wisteriaFrame.bodies.get(index);
    const right = referenceFrame.bodies.get(index);
    if (!left || !right) {
      ++missingBodies;
      update(frame, index, 1e30, 180, true);
      continue;
    }
    ++comparedBodies;
    const positionError = Math.hypot(
      left.position[0] - right.position[0],
      left.position[1] - right.position[1],
      left.position[2] - right.position[2]
    );
    const rotationError = rotationErrorDeg(
      left.rotation,
      right.rotation
    );
    update(frame, index, positionError, rotationError, false);
  }
}

console.log(
  `comparedFrames=${comparedFrames} comparedBodies=${comparedBodies} ` +
    `missingBodies=${missingBodies}`
);
if (envPath) {
  const env = JSON.parse(readFileSync(envPath, "utf8"));
  console.log(
    `reference environmentMode=${env.environmentMode} ` +
      `executionProfile=${env.executionProfile}`
  );
  console.log(
    "NOTE: reference executionProfile differs from WISTERIA " +
      "deterministic-cold-step-v1; results are observation evidence only."
  );
}
if (first) {
  console.log(
    `First divergence: frame=${first.frame} body=${first.bodyIndex} ` +
      `positionError=${first.positionError} ` +
      `rotationErrorDeg=${first.rotationErrorDeg} ` +
      `${first.missing ? "MISSING" : ""}`
  );
}
if (max) {
  console.log(
    `Maximum divergence: frame=${max.frame} body=${max.bodyIndex} ` +
      `positionError=${max.positionError} ` +
      `rotationErrorDeg=${max.rotationErrorDeg} ` +
      `${max.missing ? "MISSING" : ""}`
  );
}
process.exit(0);
