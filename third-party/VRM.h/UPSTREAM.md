# VRM.h upstream record

- Upstream: https://github.com/infosia/VRM.h
- Vendored file: `VRMC/VRM.h`
- Commit: `fee0450512ef83d7cbbe97937dcbe95325215299`
- Date: 2026-07-05
- License: MIT (`LICENSE`)
- WISTERIA usage: VRM 0.x / 1.0 extension parsing only. Rendering,
  animation, humanoid mapping and spring-bone runtime remain WISTERIA-owned.

Only the single header and license are vendored. The upstream repository also
contains optional glTF loaders (fx-gltf / cgltf / nlohmann); WISTERIA uses
its own miniz + nlohmann/json integration and does not vendor those copies.
