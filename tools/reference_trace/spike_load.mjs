import { NullEngine } from "@babylonjs/core/Engines/nullEngine.js";
import { Scene } from "@babylonjs/core/scene.js";
import { readFileSync } from "node:fs";

const pmxPath = process.argv[2];
if (!pmxPath) {
  console.error("usage: node spike1.mjs <model.pmx>");
  process.exit(2);
}

const bytes = readFileSync(pmxPath);
const engine = new NullEngine();
const scene = new Scene(engine);

// Pure entry points avoid the browser-only wasm-rayon worker helpers.
const { PmxLoader, RegisterPmxLoader } = await import(
  "babylon-mmd/esm/Loader/pmxLoader.pure.js"
);
RegisterPmxLoader();
const plugin = new PmxLoader(undefined, {
  loadReferenceFiles: false,
  preserveSerializationData: false,
  materialBuilder: {
    buildMaterials: async (
      _uniqueId,
      _materialsInfo,
      _texturesInfo,
      _imagePathTable,
      _rootUrl,
      _fileRootId,
      _referenceFiles,
      _referencedMeshes,
      _meshes,
      _scene,
      _assetContainer,
      _textureNameMap,
      _logger,
      _onTextureLoadProgress,
      onTextureLoadComplete
    ) => {
      onTextureLoadComplete?.();
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
    (_request, error) => reject(error)
  );
});

console.log("parsed keys:", Object.keys(state));
const parsed = await plugin._parseFileAsync(state.arrayBuffer);
console.log(
  "modelObject keys:",
  Object.keys(parsed),
  "materials:",
  parsed.materials?.length,
  "bones:",
  parsed.bones?.length
);

for (const method of [
  "_buildGeometryAsync",
  "_buildMaterialAsync",
  "_buildSkeletonAsync",
  "_buildMorphAsync"
]) {
  const original = plugin[method].bind(plugin);
  plugin[method] = async (...args) => {
    console.log("->", method);
    const result = await original(...args);
    console.log("<-", method);
    return result;
  };
}

const result = await plugin.importMeshAsync("", scene, state, "");

console.log(
  "loaded meshes:",
  result.meshes.length,
  "rootNodes:",
  result.transformNodes?.length ?? 0
);
const mmdMeshes = result.meshes.filter(
  (m) => m.getClassName?.() === "MmdSkinnedMesh"
);
console.log("mmd skinned meshes:", mmdMeshes.length);
for (const mesh of result.meshes) {
  const geometry = mesh.geometry;
  const buffer = mesh.getVertexBuffer?.(0);
  console.log(
    "mesh:",
    mesh.name,
    "class:",
    mesh.getClassName?.(),
    "vtxBuf:",
    buffer ? buffer.getSize() : "none",
    "bones:",
    mesh.skeleton?.bones?.length ?? "none"
  );
}
process.exit(0);
