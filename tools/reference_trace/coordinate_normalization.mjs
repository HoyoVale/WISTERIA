// R1.3B Phase 0B: ReferenceCoordinateNormalization v1 golden test.
//
// Verifies the frozen reflection formulas (contract §5):
//
//   S = diag(1, 1, -1)
//   H = diag(1, 1, -1, 1)
//
//   position:        p'  = S p
//   linear velocity: v'  = S v
//   rotation basis:  R'  = S R S
//   bone transform:  T'  = H T H
//   angular velocity:ω'  = det(S) · S · ω = -S · ω
//
// Every expected value is computed by an independent dense matrix path
// (full S·M·S multiplication), never by the fast-path formula being tested.

const EPSILON = 1e-9;

// diag(1, 1, -1)
import {
  normalizePosition,
  normalizeLinearVelocity,
  normalizeAngularVelocity,
  normalizeRotationBasis,
  normalizeBoneTransform
} from "./coordinate_normalization_lib.mjs";

const S = [1, 0, 0, 0, 1, 0, 0, 0, -1];

const degToRad = (deg) => (deg * Math.PI) / 180;

// --- independent dense paths ---
const identity4 = () => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];

const mulMat4 = (a, b) => {
  const out = new Array(16).fill(0);
  for (let c = 0; c < 4; ++c) {
    for (let row = 0; row < 4; ++row) {
      let sum = 0;
      for (let k = 0; k < 4; ++k) {
        sum += a[k * 4 + row] * b[c * 4 + k];
      }
      out[c * 4 + row] = sum;
    }
  }
  return out;
};

const mat4FromBasis = (r) => {
  const t = identity4();
  for (let c = 0; c < 3; ++c) {
    for (let row = 0; row < 3; ++row) {
      t[c * 4 + row] = r[c * 3 + row];
    }
  }
  return t;
};

const basisFromMat4 = (t) => {
  const r = new Array(9);
  for (let c = 0; c < 3; ++c) {
    for (let row = 0; row < 3; ++row) {
      r[c * 3 + row] = t[c * 4 + row];
    }
  }
  return r;
};

const mat4FromPos = (p) => {
  const t = identity4();
  t[12] = p[0];
  t[13] = p[1];
  t[14] = p[2];
  return t;
};

const rotationX = (deg) => {
  const a = degToRad(deg);
  const c = Math.cos(a);
  const s = Math.sin(a);
  const t = identity4();
  t[5] = c;
  t[6] = s;
  t[9] = -s;
  t[10] = c;
  return t;
};

const rotationY = (deg) => {
  const a = degToRad(deg);
  const c = Math.cos(a);
  const s = Math.sin(a);
  const t = identity4();
  t[0] = c;
  t[2] = -s;
  t[8] = s;
  t[10] = c;
  return t;
};

const rotationZ = (deg) => {
  const a = degToRad(deg);
  const c = Math.cos(a);
  const s = Math.sin(a);
  const t = identity4();
  t[0] = c;
  t[1] = s;
  t[4] = -s;
  t[5] = c;
  return t;
};

const denseBasis = (r) =>
  basisFromMat4(
    mulMat4(mulMat4(mat4FromBasis(S), mat4FromBasis(r)), mat4FromBasis(S))
  );

let failed = false;

const checkVector = (name, actual, expected) => {
  const ok = actual.every((v, i) => Math.abs(v - expected[i]) < EPSILON);
  if (!ok) failed = true;
  console.log(
    `${ok ? "PASS" : "FAIL"} ${name}: [${actual.map((v) => v.toFixed(6)).join(", ")}]`
  );
};

// --- translation ---
checkVector("position (1,2,3)", normalizePosition([1, 2, 3]), [1, 2, -3]);

// --- linear velocity ---
checkVector(
  "linear velocity (0.1,-0.2,0.3)",
  normalizeLinearVelocity([0.1, -0.2, 0.3]),
  [0.1, -0.2, -0.3]
);

// --- angular velocity (axial vector: -S·ω) ---
checkVector("angular velocity +X", normalizeAngularVelocity([1, 0, 0]), [-1, 0, 0]);
checkVector("angular velocity +Y", normalizeAngularVelocity([0, 1, 0]), [0, -1, 0]);
checkVector("angular velocity +Z", normalizeAngularVelocity([0, 0, 1]), [0, 0, 1]);
checkVector(
  "angular velocity (0.1,-0.2,0.3)",
  normalizeAngularVelocity([0.1, -0.2, 0.3]),
  [-0.1, 0.2, 0.3]
);

// --- rotation basis: X / Y / Z / combined ---
for (const [name, rotation] of [
  ["rotation X=30", rotationX(30)],
  ["rotation Y=45", rotationY(45)],
  ["rotation Z=60", rotationZ(60)],
  ["combined X30*Y45*Z60", mulMat4(mulMat4(rotationX(30), rotationY(45)), rotationZ(60))]
]) {
  const basis = basisFromMat4(rotation);
  checkVector(
    `rotation basis ${name}`,
    normalizeRotationBasis(basis),
    denseBasis(basis)
  );
}

// --- bone transform (translation * rotation), dense 4x4 path ---
{
  const transform = mulMat4(mat4FromPos([1, 2, 3]), rotationX(30));
  // H = diag(1, 1, -1, 1) as a dense 4x4.
  const hMat = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1];
  const expectedDense = mulMat4(
    mulMat4(hMat, transform),
    hMat
  );
  checkVector(
    "bone transform T'=H·T·H",
    normalizeBoneTransform(transform),
    expectedDense
  );
}

// --- round-trip invariants: S² = I, H² = I ---
{
  const roundTripBasis = normalizeRotationBasis(normalizeRotationBasis([1, 0, 0, 0, 1, 0, 0, 0, 1]));
  checkVector("round-trip rotation basis (S²=I)", roundTripBasis, [1, 0, 0, 0, 1, 0, 0, 0, 1]);
  const roundTripPos = normalizePosition(normalizePosition([1, 2, 3]));
  checkVector("round-trip position (S²=I)", roundTripPos, [1, 2, 3]);
  const roundTripAngular = normalizeAngularVelocity(normalizeAngularVelocity([0.1, -0.2, 0.3]));
  checkVector("round-trip angular velocity (S²=I)", roundTripAngular, [0.1, -0.2, 0.3]);
}

if (failed) {
  console.error("coordinate normalization golden test FAILED");
  process.exit(1);
}
console.log("coordinate normalization golden test PASSED");
process.exit(0);
