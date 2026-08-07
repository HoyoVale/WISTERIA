// R1.3B Phase 0B: ReferenceCoordinateNormalization v1 fast-path functions.
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
// Rotation bases and bone transforms are column-major flat arrays
// (9 / 16 floats), matching the WISTERIA trace convention.

export const normalizePosition = (p) => [p[0], p[1], -p[2]];

export const normalizeLinearVelocity = (v) => [v[0], v[1], -v[2]];

export const normalizeAngularVelocity = (w) => [-w[0], -w[1], w[2]];

// R'[c*3+r] = s[r] * R[c*3+r] * s[c], s = diag(1, 1, -1)
export const normalizeRotationBasis = (r) => {
  const out = new Array(9);
  for (let c = 0; c < 3; ++c) {
    for (let row = 0; row < 3; ++row) {
      const sRow = row === 2 ? -1 : 1;
      const sCol = c === 2 ? -1 : 1;
      out[c * 3 + row] = sRow * r[c * 3 + row] * sCol;
    }
  }
  return out;
};

// T'[c*4+r] = h[r] * T[c*4+r] * h[c], h = diag(1, 1, -1, 1)
export const normalizeBoneTransform = (t) => {
  const out = new Array(16);
  for (let c = 0; c < 4; ++c) {
    for (let row = 0; row < 4; ++row) {
      const hRow = row === 2 ? -1 : 1;
      const hCol = c === 2 ? -1 : 1;
      out[c * 4 + row] = hRow * t[c * 4 + row] * hCol;
    }
  }
  return out;
};
