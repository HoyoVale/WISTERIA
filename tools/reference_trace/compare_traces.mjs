// R1.3B Phase 0B Step 8-9 Evidence Integrity Closure: cross-implementation
// trace comparison with corpus identity gating.
//
// usage:
//   node compare_traces.mjs --wisteria <trace.jsonl> --reference <bodies.csv>
//       [--env <env.json>]
//       [--corpus <corpus.json> --asset <corpus-asset-XX>]
//
// With --corpus/--asset the comparator becomes an evidence gate:
//   - reference env model/motion SHA-256 must equal the corpus registry;
//   - WISTERIA first-frame model/motion FNV-1a64 must equal the registry;
//   - the corpus comparison points must be present on both sides;
//   - frame sets must be symmetric (missing frame on either side is invalid).
// Any integrity violation prints EVIDENCE INVALID and exits 1.
//
// Rotation bases are validated (finite, unit columns, orthogonal, det=+1);
// invalid bases are reported as INVALID_ROTATION_BASIS, never as 0 degrees.

import { readFileSync } from "node:fs";

const EPSILON = 1e-6;
const BASIS_TOLERANCE = 1e-2;

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
const corpusPath = args.includes("--corpus")
  ? args[args.indexOf("--corpus") + 1]
  : null;
const assetId = args.includes("--asset")
  ? args[args.indexOf("--asset") + 1]
  : null;
const noMotion = args.includes("--no-motion");

let evidenceInvalid = false;

const invalid = (message) => {
  evidenceInvalid = true;
  console.log(`EVIDENCE INVALID: ${message}`);
};

const column = (m, c) => [m[c * 3], m[c * 3 + 1], m[c * 3 + 2]];
const length = (v) =>
  Math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
const dot = (x, y) => x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
const cross = (x, y) => [
  x[1] * y[2] - x[2] * y[1],
  x[2] * y[0] - x[0] * y[2],
  x[0] * y[1] - x[1] * y[0]
];

const validateBasis = (m) => {
  if (!m.every((v) => Number.isFinite(v))) return "non-finite";
  const c0 = column(m, 0);
  const c1 = column(m, 1);
  const c2 = column(m, 2);
  for (const c of [c0, c1, c2]) {
    const l = length(c);
    if (Math.abs(l - 1) > BASIS_TOLERANCE)
      return `column length ${l.toFixed(4)} != 1`;
  }
  if (Math.abs(dot(c0, c1)) > BASIS_TOLERANCE ||
      Math.abs(dot(c0, c2)) > BASIS_TOLERANCE ||
      Math.abs(dot(c1, c2)) > BASIS_TOLERANCE) {
    return "columns not orthogonal";
  }
  const det = dot(c0, cross(c1, c2));
  if (Math.abs(det - 1) > BASIS_TOLERANCE)
    return `determinant ${det.toFixed(4)} != +1`;
  return null;
};

const rotationErrorDeg = (a, b) => {
  if (a.every((v, i) => v === b[i])) return 0;
  const ca = [0, 1, 2].map((c) => column(a, c));
  const cb = [0, 1, 2].map((c) => column(b, c));
  let trace = 0;
  for (let c = 0; c < 3; ++c) {
    trace += dot(ca[c], cb[c]);
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
    frames.set(frame.frame, {
      bodies,
      canonical: frame.canonical,
      modelHash: frame.modelHash,
      motionHash: frame.motionHash
    });
  }
  return frames;
};

const readReference = (path) => {
  const lines = readFileSync(path, "utf8").split("\n");
  const header = lines[0].trim().split(",");
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

// --- corpus identity gate ---
if (corpusPath && assetId) {
  const corpus = JSON.parse(readFileSync(corpusPath, "utf8"));
  const asset = corpus.assets.find((entry) => entry.id === assetId);
  if (!asset) {
    invalid(`asset ${assetId} not found in corpus registry`);
  } else {
    if (envPath) {
      const env = JSON.parse(readFileSync(envPath, "utf8"));
      const expectedModel = `sha256-${asset.pmx.sha256}`;
      if (env.modelHash !== expectedModel) {
        invalid(
          `reference modelHash ${env.modelHash} != registry ${expectedModel}`
        );
      }
      if (noMotion) {
        if ((env.motionHash ?? null) !== null) {
          invalid(
            `reference motionHash ${env.motionHash} != null for no-motion run`
          );
        }
      } else {
        const expectedMotion = asset.motion
          ? `sha256-${asset.motion.sha256}`
          : null;
        if ((env.motionHash ?? null) !== expectedMotion) {
          invalid(
            `reference motionHash ${env.motionHash} != registry ${expectedMotion}`
          );
        }
      }
    }
    const firstFrame = [...wisteria.values()][0];
    if (!firstFrame) {
      invalid("WISTERIA trace is empty");
    } else {
      if (firstFrame.modelHash !== asset.pmx.fnv1a64) {
        invalid(
          `WISTERIA modelHash ${firstFrame.modelHash} != registry ` +
            asset.pmx.fnv1a64
        );
      }
      if (noMotion) {
        if (firstFrame.motionHash !== "0000000000000000") {
          invalid(
            `WISTERIA motionHash ${firstFrame.motionHash} != zeros for no-motion run`
          );
        }
      } else {
        const expectedMotionFnv = asset.motion
          ? asset.motion.fnv1a64
          : "0000000000000000";
        if (firstFrame.motionHash !== expectedMotionFnv) {
          invalid(
            `WISTERIA motionHash ${firstFrame.motionHash} != registry ` +
              expectedMotionFnv
          );
        }
      }
    }
    for (const point of corpus.environment.comparisonPoints) {
      if (!wisteria.has(point) || !reference.has(point)) {
        invalid(`comparison point ${point} missing on one side`);
      }
    }
  }
}

// --- symmetric frame set ---
const allFrames = new Set([...wisteria.keys(), ...reference.keys()]);
let comparedFrames = 0;
let missingWisteriaFrames = 0;
let missingReferenceFrames = 0;
for (const frame of allFrames) {
  if (!wisteria.has(frame)) {
    invalid(`frame ${frame} missing in WISTERIA trace`);
    ++missingWisteriaFrames;
  }
  if (!reference.has(frame)) {
    invalid(`frame ${frame} missing in reference trace`);
    ++missingReferenceFrames;
  }
  if (wisteria.has(frame) && reference.has(frame)) {
    ++comparedFrames;
  }
}

let first = null;
let max = null;
let comparedBodies = 0;
let missingBodies = 0;
let invalidBases = 0;

const update = (frame, bodyIndex, positionError, rotationErrorDeg, missing) => {
  if (!first &&
      (positionError > EPSILON || rotationErrorDeg > EPSILON || missing)) {
    first = { frame, bodyIndex, positionError, rotationErrorDeg, missing };
  }
  if (!max || positionError > max.positionError) {
    max = { frame, bodyIndex, positionError, rotationErrorDeg, missing };
  }
};

for (const frame of allFrames) {
  const wisteriaFrame = wisteria.get(frame);
  const referenceFrame = reference.get(frame);
  if (!wisteriaFrame || !referenceFrame) continue;
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
    const leftBasisError = validateBasis(left.rotation);
    const rightBasisError = validateBasis(right.rotation);
    if (leftBasisError || rightBasisError) {
      ++invalidBases;
      invalid(
        `INVALID_ROTATION_BASIS frame=${frame} body=${index} ` +
          `(W: ${leftBasisError ?? "ok"}, R: ${rightBasisError ?? "ok"})`
      );
      update(frame, index, positionError, 180, false);
      continue;
    }
    const rotationError = rotationErrorDeg(left.rotation, right.rotation);
    update(frame, index, positionError, rotationError, false);
  }
}

console.log(
  `comparedFrames=${comparedFrames} comparedBodies=${comparedBodies} ` +
    `missingBodies=${missingBodies} ` +
    `missingWisteriaFrames=${missingWisteriaFrames} ` +
    `missingReferenceFrames=${missingReferenceFrames} ` +
    `invalidBases=${invalidBases}`
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
if (evidenceInvalid) {
  console.error("evidence integrity check FAILED");
  process.exit(1);
}
console.log("evidence integrity check PASSED");
process.exit(0);
