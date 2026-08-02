# Bullet Physics upstream

WISTERIA vendors the official Bullet Physics source in this directory.

- Upstream: `bulletphysics/bullet3`
- Release tag: `3.25`
- Commit: `2c204c49e56ed15ec5fcfa71d199ab6d6570b3f5`
- License: zlib (`LICENSE.txt` from upstream)

The source tree is intentionally not downloaded by CMake. Run:

```powershell
.\script\setup_bullet.ps1
```

The script performs a shallow sparse clone of the fixed tag, verifies the exact
commit, and copies only the upstream `src` tree, `LICENSE.txt`, and `VERSION` into this project. Commit
the resulting `third-party/bullet3` directory so future builds stay offline and
reproducible.

