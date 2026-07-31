"""Convert a static GLB into OBJ, MTL and external texture files.

Run through Blender rather than the system Python:

    blender --background --python script/convert_glb_to_obj.py -- \
        --input /path/to/model.glb \
        --output /path/to/model.obj
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

import bpy


def parse_arguments() -> argparse.Namespace:
    try:
        separator = sys.argv.index("--")
    except ValueError as error:
        raise SystemExit("Missing '--' before converter arguments") from error

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(sys.argv[separator + 1 :])


def clear_scene() -> None:
    if bpy.context.object is not None and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)


def supported_operator_arguments(operator, requested: dict) -> dict:
    properties = {
        prop.identifier for prop in operator.get_rna_type().properties
    }
    return {
        name: value
        for name, value in requested.items()
        if name in properties
    }


def import_glb(input_path: Path) -> list:
    result = bpy.ops.import_scene.gltf(filepath=str(input_path))
    if "FINISHED" not in result:
        raise RuntimeError(f"GLB import did not finish: {result}")

    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not meshes:
        raise RuntimeError("GLB import produced no mesh objects")
    return meshes


def unpack_textures(output_directory: Path) -> int:
    output_directory.mkdir(parents=True, exist_ok=True)
    unpacked_count = 0
    used_names: set[str] = set()

    for image in bpy.data.images:
        packed_file = image.packed_file
        if packed_file is None or image.size[0] <= 0 or image.size[1] <= 0:
            continue

        source_suffix = Path(image.filepath).suffix.lower()
        suffix = source_suffix if source_suffix else ".png"
        base_name = Path(image.name).stem or f"texture_{unpacked_count}"
        file_name = f"{base_name}{suffix}"
        duplicate_index = 1
        while file_name.casefold() in used_names:
            file_name = f"{base_name}_{duplicate_index}{suffix}"
            duplicate_index += 1
        used_names.add(file_name.casefold())

        texture_path = output_directory / file_name
        texture_path.write_bytes(bytes(packed_file.data))
        image.filepath = str(texture_path)
        image.filepath_raw = str(texture_path)
        unpacked_count += 1

    if unpacked_count == 0:
        raise RuntimeError("GLB contained no packed image textures")
    print(f"Unpacked {unpacked_count} texture image(s) into {output_directory}")
    return unpacked_count


def export_obj(meshes: list, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)

    bpy.ops.object.select_all(action="DESELECT")
    for mesh in meshes:
        mesh.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]

    arguments = supported_operator_arguments(
        bpy.ops.export_scene.obj,
        {
            "filepath": str(output_path),
            "use_selection": True,
            "use_mesh_modifiers": True,
            "use_normals": True,
            "use_uvs": True,
            "use_materials": True,
            "use_triangles": True,
            "keep_vertex_order": True,
            "group_by_object": True,
            "group_by_material": False,
            "axis_forward": "-Z",
            "axis_up": "Y",
            "path_mode": "RELATIVE",
        },
    )
    result = bpy.ops.export_scene.obj(**arguments)
    material_path = output_path.with_suffix(".mtl")
    if (
        "FINISHED" not in result
        or not output_path.is_file()
        or not material_path.is_file()
    ):
        raise RuntimeError(f"OBJ export failed: {result}")

    print(
        f"Exported {output_path} ({output_path.stat().st_size} bytes) and "
        f"{material_path.name} ({material_path.stat().st_size} bytes)"
    )


def main() -> None:
    arguments = parse_arguments()
    input_path = arguments.input.resolve()
    output_path = arguments.output.resolve()

    if not input_path.is_file():
        raise FileNotFoundError(f"GLB input does not exist: {input_path}")
    if output_path.suffix.lower() != ".obj":
        raise ValueError("Output path must use the .obj extension")

    clear_scene()
    meshes = import_glb(input_path)
    unpack_textures(output_path.parent)
    export_obj(meshes, output_path)


if __name__ == "__main__":
    main()
