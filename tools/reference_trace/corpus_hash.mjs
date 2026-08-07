// R1.3B Phase 0B Step 8: corpus identity hashing.
//
// Prints both identities for every input file:
//   sha256      cross-implementation canonical asset identity (contract §4.2)
//   fnv1a64     WISTERIA R1.2C internal deterministic asset identity
//               (FNV-1a 64 over raw file bytes, same constants as
//                SabaMmdRuntimeModel::HashFileBytes)
//
// usage: node corpus_hash.mjs <file> [<file> ...]

import { readFileSync } from "node:fs";
import { createHash } from "node:crypto";

const FNV_OFFSET_BASIS = 14695981039346656037n;
const FNV_PRIME = 1099511628211n;
const MASK = (1n << 64n) - 1n;

const fnv1a64Hex = (bytes) => {
  let state = FNV_OFFSET_BASIS;
  for (const byte of bytes) {
    state ^= BigInt(byte);
    state = (state * FNV_PRIME) & MASK;
  }
  return state.toString(16).padStart(16, "0");
};

for (const path of process.argv.slice(2)) {
  const bytes = readFileSync(path);
  const sha256 = createHash("sha256").update(bytes).digest("hex");
  console.log(
    `${path}\tsha256=${sha256}\tfnv1a64=${fnv1a64Hex(bytes)}`
  );
}
