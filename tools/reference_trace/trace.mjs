// R1.3B Phase 0B: babylon-mmd reference trace exporter.
//
// Clock semantics (contract §4.1): the loop advances one motionFrame at a
// time. motionFrame 0 is a prepared boundary (0 physics ticks); every N>=1
// samples the animation at the absolute 30fps frame N (babylon-mmd
// beforePhysics takes the absolute frame number) and executes exactly 4
// fixed 120Hz physics ticks.
//
// Outputs:
//   <out>.csv           aggregate mesh bounds per sampled motionFrame
//   <out>.bodies.csv    per-rigid-body world transform + velocities,
//                       keyed by sourceRigidBodyIndex (PMX index)
//   <out>.env.json      environment/identity header (contract §4.3)
//
// All reference-side coordinates are normalized to the WISTERIA canonical
// coordinate (ReferenceCoordinateNormalization v1, contract §5).

import { NullEngine } from "@babylonjs/core/Engines/nullEngine.js";
import { Scene } from "@babylonjs/core/scene.js";
import { Vector3 } from "@babylonjs/core/Maths/math.vector.js";
import "@babylonjs/core/Physics/physicsEngineComponent.js";
import { MeshBuilder } from "@babylonjs/core/Meshes/meshBuilder.js";
import { PhysicsImpostor } from "@babylonjs/core/Physics/v1/physicsImpostor.js";
import { readFileSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import Ammo from "ammojs-typed";
import { prepareFrame0, stepMotionFrame } from "./frame_driver.mjs";

// Pinned source commit (see README "冻结身份").
const PINNED_SOURCE_COMMIT = "3f523d392c176d5c9c9f9264f622d0631c1d298e";

const sha256Hex = (bytes) =>
  createHash("sha256").update(bytes).digest("hex");

const readPinnedIdentity = () => {
  const packageJson = JSON.parse(
    readFileSync(`${process.cwd()}/package.json`, "utf8")
  );
  const lock = JSON.parse(
    readFileSync(`${process.cwd()}/package-lock.json`, "utf8")
  );
  const integrity = (name) =>
    lock.packages[`node_modules/${name}`]?.integrity ?? null;
  return {
    referencePackageName: "babylon-mmd",
    referencePackageVersion: packageJson.dependencies["babylon-mmd"],
    referencePackageIntegrity: integrity("babylon-mmd"),
    corePackageName: "@babylonjs/core",
    corePackageVersion: packageJson.dependencies["@babylonjs/core"],
    corePackageIntegrity: integrity("@babylonjs/core"),
    physicsPackageName: "ammojs-typed",
    physicsPackageVersion: packageJson.dependencies["ammojs-typed"],
    physicsPackageIntegrity: integrity("ammojs-typed")
  };
};

async function main() {
  const pmxPath = process.argv[2];
  if (!pmxPath) {
    console.error(
      "usage: node bundle_trace.cjs <model.pmx> [out.csv] [motionFrames] " +
        "[sampleInterval] [vmdPath] [environmentMode]"
    );
    process.exit(2);
  }
  const outPath = process.argv[3] ?? "reference_trace.csv";
  const totalMotionFrames = Number(process.argv[4] ?? 300);
  const sampleInterval = Number(process.argv[5] ?? 1);
  const vmdPath = process.argv[6] ?? null;
  const environmentMode = process.argv[7] ?? "NormalizedComparison";
  if (!Number.isInteger(totalMotionFrames) || totalMotionFrames < 0) {
    throw new Error("motionFrames must be an integer >= 0");
  }
  if (!Number.isInteger(sampleInterval) || sampleInterval < 1) {
    throw new Error("sampleInterval must be an integer >= 1");
  }
  const dt = 1.0 / 120.0;
  if (environmentMode !== "NormalizedComparison") {
    throw new Error(
      `environmentMode "${environmentMode}" is not implemented yet; ` +
        "NativeCompatibilityAudit requires the native gravity/ground/" +
        "solver defaults to be preserved (contract §4.3)"
    );
  }

  const pmxBytes = readFileSync(pmxPath);
  const modelHash = sha256Hex(pmxBytes);
  const identity = readPinnedIdentity();

  const engine = new NullEngine();
  const scene = new Scene(engine);
  const ammo = await Ammo();

  const { MmdAmmoJSPlugin } = await import(
    "babylon-mmd/esm/Runtime/Physics/mmdAmmoJSPlugin.js"
  );
  const physicsPlugin = new MmdAmmoJSPlugin(true, ammo);
  physicsPlugin.setMaxSteps(120);
  physicsPlugin.setFixedTimeStep(1 / 120);
  scene.enablePhysics(new Vector3(0, -98.0, 0), physicsPlugin);

  // MMD always has a floor at y=0 (saba adds a static plane); babylon-mmd
  // does not, so without this the dynamic bodies fall forever. This is a
  // NormalizedComparison ground policy; the native absence is studied under
  // NativeCompatibilityAudit.
  const ground = MeshBuilder.CreateGround(
    "mmdGround",
    { width: 200, height: 200 },
    scene
  );
  ground.physicsImpostor = new PhysicsImpostor(
    ground,
    PhysicsImpostor.BoxImpostor,
    { mass: 0, friction: 0.5, restitution: 0 },
    scene
  );

  const { PmxLoader, RegisterPmxLoader } = await import(
    "babylon-mmd/esm/Loader/pmxLoader.pure.js"
  );
  RegisterPmxLoader();
  const plugin = new PmxLoader(undefined, {
    loadReferenceFiles: false,
    preserveSerializationData: false,
    buildSkeleton: true,
    buildMorph: false,
    materialBuilder: {
      buildMaterials: async (
        _id,
        _mi,
        _ti,
        _it,
        _root,
        _fid,
        _ref,
        _meshes,
        _ms,
        _sc,
        _ac,
        _map,
        _logger,
        _prog,
        onComplete
      ) => {
        onComplete?.();
        return [];
      }
    }
  });

  const state = await new Promise((resolve, reject) => {
    plugin.loadFile(
      scene,
      new Uint8Array(pmxBytes),
      "",
      (data) => resolve(data),
      undefined,
      true,
      undefined,
      (_r, e) => reject(e)
    );
  });
  const result = await plugin.importMeshAsync("", scene, state, "");
  const meshes = result.meshes.filter((m) => m.geometry);

  const parsedObject = await plugin._parseFileAsync(state.arrayBuffer);
  const rigidBodyCount = parsedObject.rigidBodies?.length ?? 0;
  console.log("rigid body count:", rigidBodyCount);
  const modes = [0, 0, 0];
  let dynamicMassMin = Infinity;
  let dynamicMassMax = -Infinity;
  let dynamicCount = 0;
  for (const rigidBody of parsedObject.rigidBodies ?? []) {
    const mode = rigidBody.physicsMode ?? rigidBody.mode;
    if (mode !== undefined) {
      modes[mode] = (modes[mode] ?? 0) + 1;
    }
    if (mode === 1 || mode === 2) {
      dynamicCount += 1;
      dynamicMassMin = Math.min(dynamicMassMin, rigidBody.mass);
      dynamicMassMax = Math.max(dynamicMassMax, rigidBody.mass);
    }
  }
  console.log(
    "rigid body modes (0=follow,1=physics,2=physics+merge):",
    modes.join("/"),
    "dynamic count:",
    dynamicCount,
    "dynamic mass range:",
    Number.isFinite(dynamicMassMin)
      ? dynamicMassMin.toFixed(2)
      : "n/a",
    "-",
    Number.isFinite(dynamicMassMax)
      ? dynamicMassMax.toFixed(2)
      : "n/a"
  );

  // Bind-pose reference (read before any stepping).
  const bind = meshes.map(
    (mesh) => mesh.geometry.getVertexBuffers().position.getData()
  );
  const withMeta = result.meshes.find((m) => m.metadata?.skeleton);
  const skinBones = meshes.map((mesh) => {
    const buffers = mesh.geometry.getVertexBuffers();
    return {
      indices: buffers.matricesIndices.getData(),
      weights: buffers.matricesWeights.getData()
    };
  });
  const skeleton = withMeta.metadata.skeleton;
  const inverseBindMatrices = skeleton.bones.map(
    (bone) => bone.getAbsoluteInverseBindMatrix().m
  );
  const mulMatrixPoint = (world, inv, vx, vy, vz) => {
    const m = new Float32Array(16);
    for (let col = 0; col < 4; ++col) {
      for (let row = 0; row < 4; ++row) {
        let sum = 0;
        for (let k = 0; k < 4; ++k) {
          sum += world[k * 4 + row] * inv[col * 4 + k];
        }
        m[col * 4 + row] = sum;
      }
    }
    return {
      x: m[0] * vx + m[4] * vy + m[8] * vz + m[12],
      y: m[1] * vx + m[5] * vy + m[9] * vz + m[13],
      z: m[2] * vx + m[6] * vy + m[10] * vz + m[14]
    };
  };

  const { MmdRuntime } = await import("babylon-mmd/esm/Runtime/mmdRuntime.js");
  const { MmdAmmoPhysics } = await import(
    "babylon-mmd/esm/Runtime/Physics/mmdAmmoPhysics.js"
  );
  const runtime = new MmdRuntime(scene, new MmdAmmoPhysics(scene));
  const model = runtime.createMmdModelFromSkeleton(
    withMeta,
    withMeta.metadata.skeleton,
    { buildPhysics: process.env.NO_PHYSICS ? false : true }
  );

  let motionHash = null;
  if (vmdPath) {
    const { RegisterMmdRuntimeModelAnimation } = await import(
      "babylon-mmd/esm/Runtime/Animation/mmdRuntimeModelAnimation.pure.js"
    );
    RegisterMmdRuntimeModelAnimation();
    const { VmdLoader } = await import("babylon-mmd/esm/Loader/vmdLoader.js");
    const vmdBytes = readFileSync(vmdPath);
    motionHash = sha256Hex(vmdBytes);
    const loader = new VmdLoader(scene);
    const animation = await loader.loadFromBufferAsync(
      "motion",
      vmdBytes.buffer.slice(
        vmdBytes.byteOffset,
        vmdBytes.byteOffset + vmdBytes.byteLength
      )
    );
    const handle = model.createRuntimeAnimation(animation);
    model.setRuntimeAnimation(handle);
    console.log("vmd loaded:", vmdPath);
  }

  const physicsEngine = scene.getPhysicsEngine();
  const physicsModel = model._physicsModel;
  const impostors = physicsModel?._impostors ?? [];
  let unmappedRigidBodyCount = 0;
  for (let i = 0; i < impostors.length; ++i) {
    if (impostors[i] === null) ++unmappedRigidBodyCount;
  }

  const skinMesh = (meshIndex, matrices) => {
    const bindPositions = bind[meshIndex];
    const { indices, weights } = skinBones[meshIndex];
    const out = new Float32Array(bindPositions.length);
    const vertexCount = bindPositions.length / 3;
    for (let i = 0; i < vertexCount; ++i) {
      const vx = bindPositions[i * 3];
      const vy = bindPositions[i * 3 + 1];
      const vz = bindPositions[i * 3 + 2];
      let ox = 0, oy = 0, oz = 0;
      for (let b = 0; b < 4; ++b) {
        const w = weights[i * 4 + b];
        if (w <= 0) continue;
        const bi = Math.floor(indices[i * 4 + b] + 0.5);
        const base = bi * 16;
        const world = matrices.subarray(base, base + 16);
        const inv = inverseBindMatrices[bi];
        const skinned = mulMatrixPoint(world, inv, vx, vy, vz);
        ox += skinned.x * w;
        oy += skinned.y * w;
        oz += skinned.z * w;
      }
      out[i * 3] = ox;
      out[i * 3 + 1] = oy;
      out[i * 3 + 2] = oz;
    }
    return out;
  };

  const aggregate = (positionsList) => {
    let minX = Infinity, minY = Infinity, minZ = Infinity;
    let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
    for (const positions of positionsList) {
      for (let i = 0; i < positions.length; i += 3) {
        const x = positions[i], y = positions[i + 1], z = positions[i + 2];
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (z < minZ) minZ = z;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
        if (z > maxZ) maxZ = z;
      }
    }
    return { minX, minY, minZ, maxX, maxY, maxZ };
  };

  const maxDisplacement = (positionsList, bindList) => {
    let maxD = 0;
    for (let m = 0; m < positionsList.length; ++m) {
      const p = positionsList[m];
      const b = bindList[m];
      for (let i = 0; i < p.length; i += 3) {
        const dx = p[i] - b[i];
        const dy = p[i + 1] - b[i + 1];
        const dz = p[i + 2] - b[i + 2];
        const d = Math.sqrt(dx * dx + dy * dy + dz * dz);
        if (d > maxD) maxD = d;
      }
    }
    return maxD;
  };

  // Per-rigid-body row. Empirical verification on the pinned babylon-mmd
  // (corpus assets + CORE pmx_physics): ammo rigid-body world transforms
  // already match the WISTERIA canonical coordinate directly, so no
  // Z-reflection is applied here. The ReferenceCoordinateNormalization
  // formulas remain library-tested for adapters that do need them.
  const readBodyRow = (index) => {
    const impostor = impostors[index];
    if (impostor === null || impostor === undefined) return null;
    const body = impostor.physicsBody;
    if (body === null || body === undefined) return null;
    const transform = body.getWorldTransform();
    const origin = transform.getOrigin();
    const position = [origin.x(), origin.y(), origin.z()];
    const basis = transform.getBasis();
    // ammo.js getRow returns a reused temporary vector; copy every component
    // immediately after each call or later rows overwrite earlier ones.
    const row0 = basis.getRow(0);
    const row0Values = [row0.x(), row0.y(), row0.z()];
    const row1 = basis.getRow(1);
    const row1Values = [row1.x(), row1.y(), row1.z()];
    const row2 = basis.getRow(2);
    const row2Values = [row2.x(), row2.y(), row2.z()];
    const basisColMajor = [
      row0Values[0], row1Values[0], row2Values[0],
      row0Values[1], row1Values[1], row2Values[1],
      row0Values[2], row1Values[2], row2Values[2]
    ];
    const rotation = basisColMajor;
    const linear = body.getLinearVelocity();
    const angular = body.getAngularVelocity();
    const linearVelocity = [
      linear.x(),
      linear.y(),
      linear.z()
    ];
    const angularVelocity = [
      angular.x(),
      angular.y(),
      angular.z()
    ];
    return [
      position[0], position[1], position[2],
      ...rotation,
      linearVelocity[0], linearVelocity[1], linearVelocity[2],
      angularVelocity[0], angularVelocity[1], angularVelocity[2]
    ];
  };

  const boundsHeader =
    "motionFrame,physicsTick,simulatedSeconds," +
    "min_x,min_y,min_z,max_x,max_y,max_z,max_displacement\n";
  const bodiesHeader =
    "motionFrame,physicsTick,simulatedSeconds,sourceRigidBodyIndex," +
    "posX,posY,posZ," +
    "rot00,rot01,rot02,rot10,rot11,rot12,rot20,rot21,rot22," +
    "linVelX,linVelY,linVelZ,angVelX,angVelY,angVelZ\n";
  let csv = boundsHeader;
  let bodiesCsv = bodiesHeader;
  let boundsRows = 0;

  const emitFrame = (motionFrame, physicsTick, positions) => {
    const bounds = aggregate(positions);
    const displacement = maxDisplacement(positions, bind);
    // Bounds derive from the same scene-space bone matrices as the rigid
    // bodies; keep the raw convention (see readBodyRow note).
    const minZ = bounds.minZ;
    const maxZ = bounds.maxZ;
    const simulatedSeconds = physicsTick / 120;
    csv +=
      `${motionFrame},${physicsTick},${simulatedSeconds.toFixed(6)},` +
      `${bounds.minX},${bounds.minY},${minZ},` +
      `${bounds.maxX},${bounds.maxY},${maxZ},${displacement}\n`;
    ++boundsRows;
    for (let i = 0; i < impostors.length; ++i) {
      const row = readBodyRow(i);
      if (row === null) continue;
      bodiesCsv +=
        `${motionFrame},${physicsTick},${simulatedSeconds.toFixed(6)},${i},` +
        `${row.join(",")}\n`;
    }
  };

  // motionFrame 0: prepared boundary through the shared frame driver
  // (sample(0) -> initializePhysics -> afterPhysics, no physics tick).
  prepareFrame0({
    sample: () => model.beforePhysics(0),
    initialize: () => model.initializePhysics(),
    publish: () => model.afterPhysics()
  });
  {
    const matrices = model.worldTransformMatrices;
    const positions = meshes.map((_, index) => skinMesh(index, matrices));
    emitFrame(0, 0, positions);
  }

  for (let motionFrame = 1; motionFrame <= totalMotionFrames; ++motionFrame) {
    // Shared driver: sample(N) once -> exactly 4 ticks -> publish once
    // (contract §4.1; beforePhysics takes the absolute 30fps frame number).
    stepMotionFrame({
      frame: motionFrame,
      sample: () => model.beforePhysics(motionFrame),
      tick: () => physicsEngine._step(dt),
      publish: () => model.afterPhysics()
    });
    if (motionFrame % sampleInterval === 0) {
      const matrices = model.worldTransformMatrices;
      const positions = meshes.map((_, index) => skinMesh(index, matrices));
      emitFrame(motionFrame, motionFrame * 4, positions);
    }
  }

  writeFileSync(outPath, csv);
  writeFileSync(`${outPath}.bodies.csv`, bodiesCsv);
  const env = {
    environmentMode,
    executionProfile: "reference-continuous-120hz-v1",
    gravity: [0, -98, 0],
    fixedTimeStep: 1 / 120,
    groundPolicy: "synthetic-ground-box-v1",
    sourceRepositoryCommit: PINNED_SOURCE_COMMIT,
    referencePackageName: identity.referencePackageName,
    referencePackageVersion: identity.referencePackageVersion,
    referencePackageIntegrity: identity.referencePackageIntegrity,
    corePackageName: identity.corePackageName,
    corePackageVersion: identity.corePackageVersion,
    corePackageIntegrity: identity.corePackageIntegrity,
    physicsPackageName: identity.physicsPackageName,
    physicsPackageVersion: identity.physicsPackageVersion,
    physicsPackageIntegrity: identity.physicsPackageIntegrity,
    physicsBackendVersion: "ammo.js via ammojs-typed",
    modelHash: `sha256-${modelHash}`,
    motionHash: motionHash === null ? null : `sha256-${motionHash}`,
    totalMotionFrames,
    sampleInterval,
    rigidBodyCount,
    unmappedRigidBodyCount,
    availability: {
      worldTransformAvailable: true,
      interpolationTransformAvailable: false,
      motionStateAvailable: false,
      linearVelocityAvailable: true,
      angularVelocityAvailable: true,
      jointMetricsAvailable: false,
      contactTopologyAvailable: false
    }
  };
  writeFileSync(`${outPath}.env.json`, JSON.stringify(env, null, 2) + "\n");
  console.log(
    "wrote",
    outPath,
    "bounds rows:",
    boundsRows,
    "env:",
    `${outPath}.env.json`
  );
  process.exit(0);
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
