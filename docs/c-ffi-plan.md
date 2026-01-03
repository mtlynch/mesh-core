# MeshCore C Core + FFI Plan

This document describes a concrete plan to consolidate MeshCore’s byte-level serial/BLE frame parsing logic into a **single, portable C library** (“C core”), and consume it from:

- **PlatformIO / Arduino / C++** (this `mesh-core` repo)
- **Flutter / Dart** (via `dart:ffi`)
- **JavaScript / Node.js** (via **WASM**)

The goal is: **define frame layouts, constants, parsing, and serialization exactly once**, with test vectors that prevent drift.

---

## 1) Goals, Non-Goals, Constraints

### Goals

1. **Single source of truth** for:
   - protocol constants (command/response/push/error codes)
   - buffer reading/writing primitives (LE integers, C-strings, fixed arrays)
   - frame parsing and serialization
2. **Portable to all PlatformIO targets** used here:
   - ESP32 (Xtensa)
   - nRF52 (ARM Cortex-M)
   - RP2040 (ARM Cortex-M0+)
   - STM32 (ARM Cortex-M)
3. Support Flutter (Android/iOS/desktop) and NodeJS.
4. Build + test automation so we can safely refactor protocol code.

### Non-goals (initially)

- Redesigning the on-wire protocol.
- Full feature parity on day 1 (start with core frames + most-used commands).
- Adding allocation-heavy abstractions.

### Constraints

- Must work on **ESP32 Xtensa**. This makes Zig/Rust-only cores difficult (toolchain support), and favors C.
- Protocol frames include **variable-length** fields (e.g., trailing strings), so “packed struct overlay” is not sufficient everywhere.
- Must remain compatible with existing firmware/app protocol.

---

## 2) Proposed Repository Layout (upstream project)

Create a new repo (recommended): `libmeshcore-client` (to distinguish it from MeshCore’s LoRa / peer-to-peer protocol).

```
libmeshcore-client/
├── include/
│   ├── meshcore_protocol.h
│   ├── meshcore_constants.h
│   ├── meshcore_types.h
│   └── meshcore_buffer.h
├── src/
│   ├── meshcore_protocol.c
│   ├── meshcore_buffer.c
│   └── meshcore_version.c
├── platformio/
│   └── library.json
├── bindings/
│   ├── dart/
│   └── js/
│   └── python/
├── test-vectors/
│   ├── frames/
│   └── README.md
├── tests/
│   ├── test_parse_contact.c
│   └── test_roundtrip.c
├── CMakeLists.txt
├── Makefile
└── README.md
```

For this `mesh-core` repo, you will consume it via `lib_deps` (git URL) once published.

---

## 3) C API Design Principles

### 3.1 Stable C ABI

- All exported functions are `extern "C"` compatible.
- Use only fixed-width integers (`stdint.h`).
- Avoid exposing complex ownership rules.

### 3.2 No allocations required for parsing

To keep it embedded-friendly, the C core should:

- Parse into caller-provided structs/buffers
- Or provide **pointer+length views** into the original buffer (“slice views”) where safe.

### 3.3 Separate “buffer primitives” from “protocol parsing”

- `meshcore_buffer.[ch]`: safe-ish cursor-based reads/writes
- `meshcore_protocol.[ch]`: functions like `mc_parse_contact`, `mc_write_cmd_send_txt_msg`

---

## 4) Header Outline

### 4.1 Constants

`include/meshcore_constants.h`

- `MC_CMD_*` command codes (1..56)
- `MC_RESP_*` response codes
- `MC_PUSH_*` push codes (>= 0x80)
- `MC_ERR_*` device error codes
- sizes: `MC_PUBLIC_KEY_SIZE`, `MC_PUBLIC_KEY_PREFIX_SIZE`, `MC_PATH_MAX_SIZE`, `MC_NAME_MAX_SIZE`

### 4.2 Types

`include/meshcore_types.h`

Use structs for *decoded* frames. Prefer decoded forms over packed overlays.

Example (decoded contact):

```c
typedef struct {
  uint8_t  public_key[32];
  uint8_t  type;
  uint8_t  flags;
  int8_t   out_path_len;
  uint8_t  out_path[64];
  char     name[33];      // always NUL-terminated in decoded form
  uint32_t last_advert;
  int32_t  gps_lat;
  int32_t  gps_lon;
  uint32_t lastmod;
} mc_contact_t;
```

Note: In decoded structs, strings should be forced NUL-terminated for convenience.

### 4.3 Parse Results

```c
typedef enum {
  MC_PARSE_OK = 0,
  MC_PARSE_BUFFER_TOO_SMALL = -1,
  MC_PARSE_INVALID_FORMAT = -2,
  MC_PARSE_STRING_TRUNCATED = -3,
} mc_parse_result_t;
```

### 4.4 Core Functions

`include/meshcore_protocol.h`

Minimum initial set:

- Parsing:
  - `mc_parse_contact(...)`
  - `mc_parse_self_info(...)`
  - `mc_parse_contact_msg(...)`
  - `mc_parse_channel_msg(...)`
  - `mc_parse_sent(...)`
  - `mc_parse_send_confirmed(...)`
  - `mc_parse_raw_data(...)`
- Serialization:
  - `mc_write_cmd_app_start(...)`
  - `mc_write_cmd_send_txt_msg(...)`
  - `mc_write_cmd_get_contacts(...)`
  - `mc_write_cmd_device_query(...)`
  - `mc_write_cmd_send_binary_req(...)`

Also:

```c
static inline int mc_is_push(uint8_t code) { return code >= 0x80; }
```

---

## 5) Buffer Utilities

`include/meshcore_buffer.h` exposes a tiny reader/writer with bounds checking.

### Reader

```c
typedef struct {
  const uint8_t* data;
  size_t len;
  size_t pos;
} mc_reader_t;

void mc_reader_init(mc_reader_t* r, const uint8_t* data, size_t len);
mc_parse_result_t mc_read_u8(mc_reader_t* r, uint8_t* out);
mc_parse_result_t mc_read_i8(mc_reader_t* r, int8_t* out);
mc_parse_result_t mc_read_u16_le(mc_reader_t* r, uint16_t* out);
mc_parse_result_t mc_read_u32_le(mc_reader_t* r, uint32_t* out);
mc_parse_result_t mc_read_i32_le(mc_reader_t* r, int32_t* out);
mc_parse_result_t mc_read_bytes(mc_reader_t* r, uint8_t* out, size_t n);
mc_parse_result_t mc_read_cstring_fixed(mc_reader_t* r, char* out, size_t fixed_n);
size_t mc_reader_remaining(const mc_reader_t* r);
```

### Writer

```c
typedef struct {
  uint8_t* data;
  size_t cap;
  size_t pos;
} mc_writer_t;

void mc_writer_init(mc_writer_t* w, uint8_t* data, size_t cap);
int mc_write_u8(mc_writer_t* w, uint8_t v);
int mc_write_u32_le(mc_writer_t* w, uint32_t v);
int mc_write_bytes(mc_writer_t* w, const uint8_t* src, size_t n);
int mc_write_cstring_fixed(mc_writer_t* w, const char* s, size_t fixed_n);
```

This matches the existing `BufferReader` / `BufferWriter` patterns you have in JS/Dart.

---

## 6) JavaScript Consumption (WASM)

We will consume `libmeshcore-client` from JavaScript via **WASM**.

### 6.1 WASM build + JS wrapper

- Compile the C core to WASM using Emscripten.
- Provide a small JS wrapper that:
  - allocates memory in WASM heap
  - calls exported parse/write functions
  - reads results back and returns JS objects

Pros:
- One artifact works in Node + browser.

Cons:
- JS wrapper must understand struct layout/offsets.

### 6.2 Notes

- Prefer a JS API that returns decoded primitives/objects, rather than exposing raw struct memory.
- Include the WASM wrapper in cross-language test runs using the same golden binary fixtures.

---

## 7) Dart / Flutter FFI Strategy

Use `dart:ffi` with prebuilt dynamic libraries:

- Android: `.so` (arm64-v8a, armeabi-v7a, x86_64)
- iOS: static `.a` linked via CocoaPods / SPM
- macOS: `.dylib`
- Linux: `.so`
- Windows: `.dll`

Two layers:

1. **Raw bindings**: Structs + function signatures mirroring C.
2. **High-level Dart API**: converts decoded structs into Dart models.

Important: keep Dart structs aligned with C (`@Packed`, `@Array`, `@Int8`, etc.).

---

## 8) PlatformIO Integration (this repo)

### 8.1 Easiest integration: PlatformIO library

Publish `libmeshcore-client` with a `platformio/library.json` and headers under `include/`.

Then in `mesh-core`:

```ini
lib_deps =
  ...
  https://github.com/meshcore-dev/libmeshcore-client.git
```

PlatformIO will compile the `.c` sources as part of the build for **each target** using that target’s toolchain (ESP32 Xtensa, ARM, etc.).

This avoids needing prebuilt `.a` files for embedded.

### 8.2 Controlling compilation

If you need to exclude/enable modules, use `build_flags`:

```ini
build_flags =
  -DMC_ENABLE_PARSER=1
  -DMC_ENABLE_WRITER=1
```

and in C:

```c
#if MC_ENABLE_PARSER
  ...
#endif
```

---

## 9) Test Vectors (Critical)

Even with a single core, tests prevent accidental wire-format changes.

### 9.1 Golden binary fixtures

Store representative frames captured from real devices (or produced by existing implementations) in `test-vectors/frames/*.bin`.

Example vectors:

- `self_info_v8.bin`
- `contact.bin`
- `contact_msg_v3.bin`
- `raw_data.bin`
- `trace_data.bin`

### 9.2 Cross-language tests

- C tests: parse each fixture, assert field values.
- JS tests (WASM): parse same fixture, compare.
- Dart tests: parse same fixture, compare.

If you can’t run all languages in one CI job, at minimum:

- Run C tests in CI.
- Run JS WASM tests in CI.
- Run Dart tests in CI.

---

## 10) Incremental Migration Plan

1. **Create C core repo** with constants + buffer utilities.
2. Implement parsing for the top 5 frames used today:
   - SelfInfo
   - Contact
   - ContactMessage / ChannelMessage
   - Sent
   - RawData
3. Add test vectors and C unit tests.
4. Integrate into `mesh-core` as `lib_deps` and use it in one feature path.
5. Add WASM build + JS wrapper; migrate `meshcore.js` parsing first.
6. Add Dart FFI package + prebuilt libs; migrate `howdy-neighbor`.
7. Expand coverage to remaining commands/responses.
8. Remove duplicated parsing code from JS/Dart once stable.

---

## 11) Risks and Mitigations

### Risk: Struct layout differences across compilers

Mitigation:
- Avoid direct struct overlays for variable-length frames.
- For fixed fields, decode via reader functions.
- If packed structs are used, guard with `static_assert(sizeof(...))` and `#pragma pack` / `__attribute__((packed))` per compiler.

### Risk: ABI drift for Dart FFI

Mitigation:
- Version exported functions (e.g., `mc_v1_parse_contact`).
- Provide `mc_protocol_version()` function.

### Risk: JS wrapper offset bugs

Mitigation:
- Prefer returning decoded primitives rather than exposing raw struct blobs.
- Add JS tests against binary fixtures.

---

## 12) Decision Points

Before implementation, decide:

1. **JS binding choice**: WASM (portable) vs N-API (native).
2. **Dart delivery**: prebuilt binaries committed vs built in CI release assets.
3. **Scope v1**: which frames must be covered to delete duplicated logic.

---

## 12.1 Unit testing strategy (GoogleTest)

We will use **GoogleTest (gtest)** for host-based unit tests of `libmeshcore-client`.

Rationale:

- Tests run in CI on desktop toolchains (Linux/macOS/Windows) where gtest is easy to use.
- gtest is already familiar to many C++ developers.
- Although `libmeshcore-client` is written in **C**, it is straightforward to test C APIs from C++.

### Test layout

Suggested structure in the `libmeshcore-client` repo:

```
libmeshcore-client/
├── include/
├── src/
├── test-vectors/
│   └── frames/
│       ├── contact.bin
│       └── self_info_v8.bin
└── tests/
    ├── CMakeLists.txt
    ├── fixtures.h
    ├── fixtures.cpp
    ├── test_parse_contact.cpp
    ├── test_parse_self_info.cpp
    └── test_roundtrip.cpp
```

### CMake integration

Use CMake `FetchContent` to fetch gtest in CI/builds (no system install required):

```cmake
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

# For Windows: prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(mc_tests
  fixtures.cpp
  test_parse_contact.cpp
  test_parse_self_info.cpp
)

target_link_libraries(mc_tests
  PRIVATE
    meshcore_client   # your C library target
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(mc_tests)
```

### Calling C from gtest

In each gtest file, wrap the C headers:

```cpp
extern "C" {
#include "meshcore_protocol.h"
}
```

### Golden fixture tests (preferred)

Store **real captured frames** as `.bin` files in `test-vectors/frames/`.

Pattern:

1. Load fixture bytes from disk
2. Call `mc_parse_*`
3. Assert the decoded fields are correct

This is the main defense against “wire format drift”.

### CI

Run unit tests on at least:

- Ubuntu (gcc/clang)
- macOS
- Windows

Command:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Note: embedded targets (PlatformIO/Arduino) do not need to run gtest; they consume the same C sources compiled by their toolchain. The correctness is validated by host tests + golden vectors.

---

## 13) Python Client Integration (ctypes / cffi)

If you also have a Python client (e.g. the `MessageReader` in `src/meshcore/reader.py`) that currently re-implements the same byte-level parsing logic with `io.BytesIO`, `struct.unpack`, and manual slicing, it can consume **the same C core** via Python FFI.

### 13.0 Python bindings as a first-class deliverable

Treat Python as a peer consumer (like Dart/JS), not an afterthought:

- Add `bindings/python/` to the `libmeshcore-client` repo.
- Publish a Python package (e.g. `meshcore-client` or `libmeshcore-client`) to PyPI.
- Provide either:
  - a thin `cffi` ABI wrapper that loads prebuilt native binaries, or
  - wheels that bundle the right shared library for each platform.

### 13.1 Recommended binding: `cffi` (ABI mode)

For most projects, **`cffi` in ABI mode** is the best balance of:

- minimal wrapper code
- stable cross-platform behavior
- less manual struct layout work than `ctypes`

In ABI mode, you **do not** compile a Python extension; you simply ship/load the platform-specific dynamic library:

- Linux: `libmeshcore_client.so`
- macOS: `libmeshcore_client.dylib`
- Windows: `meshcore_client.dll`

Example binding module (`meshcore/_ffi.py`):

```python
from cffi import FFI

ffi = FFI()

ffi.cdef("""
typedef struct {
  uint8_t  public_key[32];
  uint8_t  type;
  uint8_t  flags;
  int8_t   out_path_len;
  uint8_t  out_path[64];
  char     name[33];
  uint32_t last_advert;
  int32_t  gps_lat;
  int32_t  gps_lon;
  uint32_t lastmod;
} mc_contact_t;

typedef int mc_parse_result_t;

mc_parse_result_t mc_parse_contact(const uint8_t* buffer, size_t len, mc_contact_t* out);
""")

lib = ffi.dlopen("libmeshcore_client.so")  # platform-specific selection in real code
```

#### Suggested `bindings/python/` layout

```
bindings/python/
├── pyproject.toml
├── README.md
└── src/
    └── meshcore_client/
        ├── __init__.py
        ├── _ffi.py            # cffi.cdef + dlopen + platform selection
        ├── protocol.py        # high-level parse/write helpers returning Python dicts
        └── native/
            ├── linux-x86_64/libmeshcore_client.so
            ├── macos-arm64/libmeshcore_client.dylib
            └── win-amd64/meshcore_client.dll
```

Where `protocol.py` exposes convenience APIs that match your existing expectations:

```python
def parse_frame(frame: bytes) -> tuple[str, dict]:
    """Return (event_type, payload_dict)"""
    ...
```

or more granular:

```python
def parse_contact(payload: bytes) -> dict: ...
def parse_self_info(payload: bytes) -> dict: ...
```

Then in `reader.py`, replace the inline parsing block with calls into `lib`:

```python
from ._ffi import ffi, lib

def _parse_contact_frame(payload: bytes) -> dict | None:
    out = ffi.new("mc_contact_t*")
    buf = ffi.new("uint8_t[]", payload)
    rc = lib.mc_parse_contact(buf, len(payload), out)
    if rc != 0:
        return None

    # Convert C struct -> Python dict (keep your existing schema)
    plen = int(out.out_path_len)
    if plen < 0:
        plen = 0
    return {
        "public_key": bytes(out.public_key).hex(),
        "type": int(out.type),
        "flags": int(out.flags),
        "out_path_len": int(out.out_path_len),
        "out_path": bytes(ffi.buffer(out.out_path, 64))[:plen].hex(),
        "adv_name": ffi.string(out.name).decode("utf-8", "ignore"),
        "last_advert": int(out.last_advert),
        "adv_lat": int(out.gps_lat) / 1e6,
        "adv_lon": int(out.gps_lon) / 1e6,
        "lastmod": int(out.lastmod),
    }
```

### 13.2 Alternative binding: `ctypes` (stdlib)

If you want **zero third-party dependencies**, use `ctypes`. This is viable, but you must define the struct layout carefully.

Rule of thumb:

- prefer decoded “safe” structs in C (NUL-terminated strings, explicit field sizes)
- keep `ctypes.Structure._fields_` aligned and add CI tests that compare parsed results against test vectors

### 13.3 Packaging the native library for Python

You have three common options:

1. **Ship binaries inside the Python wheel** (recommended for end users)
   - build wheels per platform/arch in CI
2. **Ship sources and compile at install time** (not recommended; fragile)
3. **Ship a separate “native” package** (only if you already do this elsewhere)

For (1), layout commonly looks like:

```
src/meshcore/native/
  linux-x86_64/libmeshcore_client.so
  macos-arm64/libmeshcore_client.dylib
  win-amd64/meshcore_client.dll
```

and your loader selects the correct one at runtime.

#### CI/release approach for Python

Recommended:

1. CI builds shared libraries for:
   - Linux x86_64 (manylinux)
   - macOS arm64 / x86_64
   - Windows amd64
2. CI produces Python wheels that bundle the matching native library.
3. `pip install meshcore-client` “just works”.

Tools:

- `cibuildwheel` for building wheels in GitHub Actions
- a small platform selector in `_ffi.py` that prefers system-installed libs but can fall back to bundled ones

Notes:

- If you need ARM Linux (Raspberry Pi), add wheels for `manylinux_aarch64`.
- Keep the Python package independent from `mesh-core` (firmware repo) to avoid heavyweight installs.

### 13.4 Migration plan for `MessageReader`

Your current `MessageReader.handle_rx()`:

- reads `packet_type_value` (first byte)
- then parses payload differently per `PacketType`
- dispatches events with dict payloads

The migration approach is:

1. Keep `handle_rx()` as the dispatcher/state machine.
2. Replace **per-frame parsing** bodies with calls to `mc_parse_*`.
3. Preserve the output dict schema so the rest of the Python client is unchanged.

This way, Python becomes just:

- serial/BLE transport + framing
- event dispatch
- high-level helpers (LPP decode, ACL/status decode, etc.)

and the byte-level protocol stays in one place.

---

## Appendix: Suggested Minimal v1 API Surface

Parsing:

- `mc_parse_self_info`
- `mc_parse_contact`
- `mc_parse_contact_msg`
- `mc_parse_channel_msg`
- `mc_parse_sent`
- `mc_parse_raw_data`

Writing:

- `mc_write_cmd_app_start`
- `mc_write_cmd_send_txt_msg`
- `mc_write_cmd_get_contacts`
- `mc_write_cmd_device_query`

This set covers the core companion workflow and will eliminate the most duplicated byte-level code.
