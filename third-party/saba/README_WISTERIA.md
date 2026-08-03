# Saba (WISTERIA vendored copy)

- Upstream: https://github.com/benikabocha/saba
- Vendored commit: `29b8efa8b31c8e746f9a88020fb0ad9dcdcf3332` (2023-09-21)
- License: MIT (see `LICENCE`)

## Why it is here

Saba is the primary community reference for the WISTERIA MMD adapter rewrite:
it is C++/OpenGL/Bullet, uses the same language and physics stack as WISTERIA,
and its `MMDPhysics` implementation is small and clean.

## Integration

`WISTERIA.cmake` (included from the project root) builds only the core Saba
library (`Base` + `Model/MMD`) as a static `saba` target. Saba's own viewer,
examples, gtests, OBJ and XFile loaders are intentionally excluded; WISTERIA
has its own Assimp-based importers.

The `saba` target is linked into `wisteria_core` so the future MMD compat
adapter can call it directly.

## Notes

- Only the `glfw` and `glslang` submodules are declared upstream; the remaining
  `external/` directories (glm, spdlog, json, ...) are plain vendored files in
  the repository and are present in this copy.
- This directory was fetched with a shallow clone and currently still contains
  `.git` metadata. Remove `third-party/saba/.git` before committing if a flat
  vendored tree is preferred.
