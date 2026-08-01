"""Convert one PMX model to a self-contained GLB using Blender.

Run through Blender rather than the system Python:

blender --background \
  --python script/convert_mmd.py -- \
  --input "./assets/models/mmd/爱弥斯_pmx/爱弥斯.pmx" \
  --output "./assets/models/glb/爱弥斯_glb/爱弥斯.glb" \
  --rigged

The vendored MMD Tools package in third-party/blender_mmd_tools is used by
default. Pass --addon-dir only when testing another checkout.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

import bpy


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ADDON_DIR = PROJECT_ROOT / "third-party" / "blender_mmd_tools"


def parse_arguments() -> argparse.Namespace:
    try:
        separator = sys.argv.index("--")
    except ValueError as error:
        raise SystemExit("Missing '--' before converter arguments") from error

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--addon-dir",
        default=DEFAULT_ADDON_DIR,
        type=Path,
        help=(
            "Directory containing the mmd_tools Python package "
            f"(default: {DEFAULT_ADDON_DIR})"
        ),
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--scale",
        default=0.08,
        type=float,
        help="MMD Tools import scale (default: 0.08)",
    )
    parser.add_argument(
        "--rigged",
        action="store_true",
        help="Preserve armature, skinning and morph targets (larger output)",
    )
    return parser.parse_args(sys.argv[separator + 1 :])


def register_mmd_tools(addon_dir: Path) -> None:
    addon_dir = addon_dir.resolve()
    package_dir = addon_dir / "mmd_tools"
    if not (package_dir / "__init__.py").is_file():
        raise FileNotFoundError(
            f"MMD Tools package not found below addon directory: {addon_dir}"
        )
    shared_toon_folder = package_dir / "externals" / "MikuMikuDance"
    missing_shared_toons = [
        shared_toon_folder / f"toon{index:02d}.bmp"
        for index in range(1, 11)
        if not (shared_toon_folder / f"toon{index:02d}.bmp").is_file()
    ]
    if missing_shared_toons:
        raise FileNotFoundError(
            "MMD Tools shared Toon textures are incomplete: "
            + ", ".join(str(path) for path in missing_shared_toons)
        )

    sys.path.insert(0, str(addon_dir))
    import mmd_tools  # pylint: disable=import-outside-toplevel
    from mmd_tools import bpyutils  # pylint: disable=import-outside-toplevel
    from mmd_tools.core import material  # pylint: disable=import-outside-toplevel

    mmd_tools.register()

    # Loading a vendored addon directly from sys.path does not add an entry to
    # bpy.context.preferences.addons in Blender 3.0. MMD Tools consequently
    # falls back to an empty shared Toon directory. Override that one project
    # preference while preserving every other upstream preference lookup.
    upstream_addon_preferences = bpyutils.addon_preferences

    def project_addon_preferences(name: str, default=None):
        if name == "shared_toon_folder":
            return str(shared_toon_folder)
        return upstream_addon_preferences(name, default)

    bpyutils.addon_preferences = project_addon_preferences
    material.addon_preferences = project_addon_preferences
    print(f"Using MMD shared Toon textures from {shared_toon_folder}")


def clear_scene() -> None:
    if bpy.context.object is not None and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)


def import_pmx(input_path: Path, scale: float, rigged: bool) -> None:
    import_types = {"MESH", "ARMATURE", "MORPHS"} if rigged else {"MESH"}
    result = bpy.ops.mmd_tools.import_model(
        filepath=str(input_path.resolve()),
        types=import_types,
        scale=scale,
        clean_model=True,
        remove_doubles=False,
        fix_IK_links=False,
        apply_bone_fixed_axis=False,
        rename_bones=True,
        use_underscore=False,
        use_mipmap=True,
        sph_blend_factor=1.0,
        spa_blend_factor=1.0,
        log_level="INFO",
        save_log=False,
    )
    if "FINISHED" not in result:
        raise RuntimeError(f"PMX import did not finish: {result}")

    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not meshes:
        raise RuntimeError("PMX import produced no mesh objects")

    vertex_count = sum(len(obj.data.vertices) for obj in meshes)
    triangle_count = sum(len(obj.data.polygons) for obj in meshes)
    print(
        f"Imported {input_path.name}: "
        f"{len(meshes)} mesh object(s), "
        f"{vertex_count} vertices, {triangle_count} polygons"
    )


def convert_materials_for_gltf() -> None:
    bpy.ops.object.select_all(action="DESELECT")
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    for mesh in meshes:
        mesh.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]

    result = bpy.ops.mmd_tools.convert_materials(
        use_principled=True,
        clean_nodes=True,
    )
    if "FINISHED" not in result:
        raise RuntimeError(f"MMD material conversion failed: {result}")

    image_materials = 0
    for material in bpy.data.materials:
        if material.node_tree is None:
            continue
        if any(
            node.type == "TEX_IMAGE" and node.image is not None
            for node in material.node_tree.nodes
        ):
            image_materials += 1
    print(
        f"Converted {len(bpy.data.materials)} material(s); "
        f"{image_materials} material(s) reference image textures"
    )


def supported_operator_arguments(operator, requested: dict) -> dict:
    properties = {
        prop.identifier for prop in operator.get_rna_type().properties
    }
    return {
        name: value
        for name, value in requested.items()
        if name in properties
    }


def export_glb(output_path: Path, rigged: bool) -> None:
    output_path = output_path.resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    arguments = supported_operator_arguments(
        bpy.ops.export_scene.gltf,
        {
            "filepath": str(output_path),
            "export_format": "GLB",
            "use_selection": False,
            "export_yup": True,
            "export_apply": False,
            "export_animations": False,
            "export_skins": rigged,
            "export_morph": rigged,
            "export_materials": "EXPORT",
            "export_texcoords": True,
            "export_normals": True,
        },
    )
    result = bpy.ops.export_scene.gltf(**arguments)
    if "FINISHED" not in result or not output_path.is_file():
        raise RuntimeError(f"GLB export failed: {result}")

    print(f"Exported {output_path} ({output_path.stat().st_size} bytes)")


def main() -> None:
    arguments = parse_arguments()
    input_path = arguments.input.resolve()
    if not input_path.is_file():
        raise FileNotFoundError(f"PMX input does not exist: {input_path}")
    if arguments.scale <= 0.0:
        raise ValueError("Import scale must be greater than zero")

    register_mmd_tools(arguments.addon_dir)
    clear_scene()
    import_pmx(input_path, arguments.scale, arguments.rigged)
    convert_materials_for_gltf()
    export_glb(arguments.output, arguments.rigged)


if __name__ == "__main__":
    main()
