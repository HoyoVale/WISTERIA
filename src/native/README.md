# Native facade layout

`wisteria_native` is an experimental C ABI adapter. It is deliberately kept
thin and must not become the owner of engine policy.

- `internal/native_context.*`: handle registry, lifetime leases and common
  validation helpers.
- `native_common.cpp`: ABI version, status and context lifecycle.
- `native_model.cpp`: headless model, motion, physics stepping and diagnostics.
- `native_window.cpp`: optional desktop window, input and camera facade.
- `windows_path.*`: Windows-only UTF-8 path conversion.

The public ABI remains in `include/wisteria/native/wisteria_native.h` so old
callers keep a single include. Internally, model and desktop dependencies stay
separate to make the future `wisteria_runtime_headless` extraction possible.

## Frame ownership

`wisteria_poll_and_render(context, dt_seconds)` is context-wide and must be
called exactly once per frontend frame. The old
`wisteria_window_poll_and_render` symbol is a compatibility wrapper; it does
not render only the selected window.

## Stability contract

- v0.x is experimental and does not promise ABI stability.
- A context is single-threaded; callers serialize all calls for that context.
- Desktop calls should be driven from one process UI thread because GLFW has
  process-level state and platform-specific main-thread restrictions.
- OpenGL objects never cross independent `Application` resource share groups.
