# CI

The commands live in `tools/ci.sh`, not in the workflows. This repository is
published from the pico9918 firmware repository, which runs the same jobs from
workflows of its own, and two copies of the commands would drift.

One workflow per thing being proven:

| Workflow | Jobs | Proves |
|---|---|---|
| `render.yml` | Golden frames, TMS9918A mode, Multi-instance | what the renderer computes |
| `portability.yml` | Renderer suite (x4), Warnings (gcc, clang) | it builds and renders the same everywhere |
| `bindings.yml` | Python module | the CPython extension works against the installed library |
| `docs.yml` | Doxygen, Publish the API documentation | the headers document cleanly, and publish |
| `package.yml` | Package (x4), Attach the archives to the release | the archives are consumable, and a tag ships them |

`docs` and `pages` share a workflow because the artifact is handed between them,
as do `package` and `release`.

The steps every job repeats - toolchain, Python, pillow - are in
[`../actions/env`](../actions/env/action.yml).

## What a green badge does not cover

The RP2040 / RP2350 build, which needs the Pico SDK and the firmware around it,
and what a scanline costs on a device. The suite here asserts what the renderer
computed; the firmware repository measures what it cost. Green means the library
is correct and portable, not that the firmware builds or that it fits in the line.
