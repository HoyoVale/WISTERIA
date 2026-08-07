import { NullEngine } from "@babylonjs/core/Engines/nullEngine.js";
import { Scene } from "@babylonjs/core/scene.js";
import { Vector3 } from "@babylonjs/core/Maths/math.vector.js";
import "@babylonjs/core/Physics/physicsEngineComponent.js";
import { MeshBuilder } from "@babylonjs/core/Meshes/meshBuilder.js";
import { PhysicsImpostor } from "@babylonjs/core/Physics/v1/physicsImpostor.js";
import { readFileSync, writeFileSync } from "node:fs";
import Ammo from "ammojs-typed";

async function main() {
  const pmxPath = process.argv[2];
  const outPath = process.argv[3] ?? "reference_trace.csv";
  const totalFrames = Number(process.argv[4] ?? 300);
  // Default to one row per motion frame (4 ticks); tick-based sampling can
  // be requested explicitly with a different interval.
  const sampleInterval = Number(process.argv[5] ?? 4);
  const dt = 1.0 / 120.0;
  const bytes = readFileSync(pmxPath);

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
  // does not, so without this the dynamic bodies fall forever.
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
      new Uint8Array(bytes),
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

  // Rigid body inventory: mode distribution + mass stats (PMX fields).
  {
    const rigidBodies = state.arrayBuffer
      ? null
      : null;
    const parsedObject = await plugin._parseFileAsync(state.arrayBuffer);
    console.log("rigid body count:", parsedObject.rigidBodies?.length);
    const modes = [0, 0, 0];
    let dynamicMassMin = Infinity;
    let dynamicMassMax = -Infinity;
    let dynamicCount = 0;
    for (const rb of parsedObject.rigidBodies) {
      if (rb.physicsMode === undefined && rb.mode === undefined) {
        console.log("sample rigid body keys:", Object.keys(rb));
        break;
      }
      const mode = rb.physicsMode ?? rb.mode;
      modes[mode] = (modes[mode] ?? 0) + 1;
      if (mode === 1 || mode === 2) {
        dynamicCount++;
        dynamicMassMin = Math.min(dynamicMassMin, rb.mass);
        dynamicMassMax = Math.max(dynamicMassMax, rb.mass);
      }
    }
    console.log(
      "rigid body modes (0=follow,1=physics,2=physics+merge):",
      modes.join("/"),
      "dynamic count:",
      dynamicCount,
      "dynamic mass range:",
      dynamicMassMin.toFixed(2),
      "-",
      dynamicMassMax.toFixed(2)
    );
  }

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
  // worldTransformMatrices are GLOBAL bone matrices; the shader multiplies
  // them by each bone's inverted bind matrix. Reproduce that here.
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

  const physicsEngine = scene.getPhysicsEngine();

  // GPU skinning stays on the shader, so skinned positions must be computed
  // on the CPU here: weighted bone matrices x bind position.
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

  let csv =
    "motionFrame,physicsTick,simulatedSeconds," +
    "min_x,min_y,min_z,max_x,max_y,max_z,max_displacement\n";

  // motionFrame 0 is a prepared boundary (no physics step): emit the
  // bind-pose row so the WISTERIA comparison point at tick 0 exists.
  {
    const bounds = aggregate(bind);
    csv += `0,0,0.000000,${bounds.minX},${bounds.minY},${bounds.minZ},` +
      `${bounds.maxX},${bounds.maxY},${bounds.maxZ},0\n`;
  }

  for (let frame = 0; frame < totalFrames; ++frame) {
    model.beforePhysics(dt);
    physicsEngine._step(dt);
    model.afterPhysics();
    if ((frame + 1) % sampleInterval !== 0) continue;
    const matrices = model.worldTransformMatrices;
    const positions = meshes.map((_, index) => skinMesh(index, matrices));
    const bounds = aggregate(positions);
    const displacement = maxDisplacement(positions, bind);
    const physicsTick = frame + 1;
    const motionFrame = Math.floor(physicsTick / 4);
    const simulatedSeconds = physicsTick / 120;
    csv +=
      `${motionFrame},${physicsTick},${simulatedSeconds.toFixed(6)},` +
      `${bounds.minX},${bounds.minY},${bounds.minZ},` +
      `${bounds.maxX},${bounds.maxY},${bounds.maxZ},${displacement}\n`;
  }
  writeFileSync(outPath, csv);
  console.log("wrote", outPath, "rows:", 1 + totalFrames / sampleInterval);
  process.exit(0);
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
