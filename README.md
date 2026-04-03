# boai

`boai` is a standalone OAI chat-completions facade built on top of
`bsrvcore` client APIs.

It ships as a normal CMake library package:

- package name: `boai`
- imported target: `boai::boai`
- public namespace: `boai::completion`

## Dependency Resolution

`boai` resolves `bsrvcore` in this order:

1. Existing CMake target `bsrvcore::bsrvcore`
2. Local config package via `find_package(bsrvcore CONFIG)`
3. Explicit local source tree via `BOAI_BSRVCORE_SOURCE_DIR`
4. Network fetch via `FetchContent`

You can point `boai` at a local CMake package directory directly:

```bash
cmake -S . -B build \
  -DBOAI_BSRVCORE_CMAKE_DIR=/path/to/bsrvcore/build
```

You can point `boai` at a local `bsrvcore` source tree:

```bash
cmake -S . -B build \
  -DBOAI_BSRVCORE_SOURCE_DIR=/path/to/bsrvcore
```

If you do nothing, `boai` first tries standard package discovery and then can
fetch `bsrvcore` from the network:

```bash
cmake -S . -B build
```

You can override the fetch source and ref:

```bash
cmake -S . -B build \
  -DBOAI_BSRVCORE_GIT_REPOSITORY=https://github.com/haomingbai/bsrvcore.git \
  -DBOAI_BSRVCORE_GIT_TAG=v0.13.0
```

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Install

```bash
cmake --install build --prefix /tmp/boai-install
```

## Example

See [docs/manual/oai-completion.md](docs/manual/oai-completion.md).
