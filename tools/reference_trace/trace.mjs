import { NullEngine } from "@babylonjs/core/Engines/nullEngine.js";
import { Scene } from "@babylonjs/core/scene.js";
import { Vector3 } from "@babylonjs/core/Maths/math.vector.js";
import "@babylonjs/core/Physics/physicsEngineComponent.js";
import { readFileSync, writeFileSync } from "node:fs";
import Ammo from "ammojs-typed";

async function main() {
  const pmxPath = process.argv[2];
  const outPath = process.argv[3] ?? "reference_trace.csv";
  const totalFrames = Number(process.argv[4] ?? 300);
  const sampleInterval = Number(process.argv[5] ?? 10);
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
    { buildPhysics: true }
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

  // Diagnostics: matrix convention at the bind pose (no physics step).
  model.beforePhysics(0);
  model.afterPhysics();
  const debugMatrices = model.worldTransformMatrices;
  const debugBind = bind[0];
  const debugIndices = skinBones[0].indices;
  const debugWeights = skinBones[0].weights;
  const debugSkinned = skinMesh(0, debugMatrices);
  console.log(
    "frame0 v0 bind:",
    debugBind[0].toFixed(3),
    debugBind[1].toFixed(3),
    debugBind[2].toFixed(3)
  );
  console.log(
    "frame0 v0 skin:",
    debugSkinned[0].toFixed(3),
    debugSkinned[1].toFixed(3),
    debugSkinned[2].toFixed(3)
  );
  console.log(
    "v0 bones/weights:",
    debugIndices[0],
    debugIndices[1],
    debugIndices[2],
    debugIndices[3],
    "/",
    debugWeights[0].toFixed(2),
    debugWeights[1].toFixed(2),
    debugWeights[2].toFixed(2),
    debugWeights[3].toFixed(2)
  );
  const m0 = debugMatrices.slice(0, 16);
  console.log("bone0 matrix:", m0.map((v) => v.toFixed(3)).join(" "));
  const m10 = debugMatrices.slice(10 * 16, 10 * 16 + 16);
  const m177 = debugMatrices.slice(177 * 16, 177 * 16 + 16);
  console.log("bone10 matrix:", m10.map((v) => v.toFixed(3)).join(" "));
  console.log("bone177 matrix:", m177.map((v) => v.toFixed(3)).join(" "));

  let csv = "frame,min_x,min_y,min_z,max_x,max_y,max_z,max_displacement\n";
  for (let frame = 0; frame < totalFrames; ++frame) {
    model.beforePhysics(dt);
    physicsEngine._step(dt);
    model.afterPhysics();
    if ((frame + 1) % sampleInterval !== 0) continue;
    const matrices = model.worldTransformMatrices;
    const positions = meshes.map((_, index) => skinMesh(index, matrices));
    const bounds = aggregate(positions);
    const displacement = maxDisplacement(positions, bind);
    csv +=
      `${frame + 1},${bounds.minX},${bounds.minY},${bounds.minZ},` +
      `${bounds.maxX},${bounds.maxY},${bounds.maxZ},${displacement}\n`;
  }
  writeFileSync(outPath, csv);
  console.log("wrote", outPath, "rows:", totalFrames / sampleInterval);
  process.exit(0);
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
