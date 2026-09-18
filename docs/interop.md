# Using %C with C

%C uses the native C ABI: real headers, the host linker, and no FFI wrappers or code generators. The compiler reads every declaration in one of two contexts: `.mc` sources and headers brought in via `#include`. Both pass through the same frontend parser, but dialect rules adapt based on the context.

For a quick overview of dialect rules, see [quickstart.md](quickstart.md).

## Context Model

Every declaration processed by `modc` belongs to exactly one context:

| Context    | What                                                                            |
| ---------- | ------------------------------------------------------------------------------- |
| **Source** | .mc files passed directly to modc or pulled in via import "...".                |
| **Header** | Code included via #include "..." or #include <...>, along with expanded macros. |

Classification is purely mechanical: if a file is a root compile translation unit (`.mc`), it is treated as user code. All other files are treated as headers.

## Dialect Rules vs C Headers

| Topic                     | Source                                               | Headers                               |
| ------------------------- | ---------------------------------------------------- | ------------------------------------- |
| Field access              | Dot operator only (p.x)                              | Arrow allowed (p->x)                  |
| Qualifiers and Specifiers | `const` / `const?` allowed; volatile, restrict, register rejected | Allowed                               |
| Integer Types             | Fixed-width types required (int64_t)                 | Host ABI long / long long permitted   |
| Declarations              | One declaration per statement                        | Multi-declarators allowed (int a, b;) |
| Conditional Assignment    | Requires explicit parenthesization ((x = ...))       | Standard C rules                      |
| Control Flow              | Mandatory braces for if, for, while                  | Optional braces                       |
| Switch Statements         | requires `break` or `fallthrough;`                   | Implicit fallthrough allowed          |
| Function Declarations     | Function prototypes rejected                         | Standard C prototypes allowed         |
| Vendor attributes         | Rejected                                             | Ignored (`__attribute__`, `__declspec`, `__asm` / Darwin aliases, calling conventions, …) |
| Tag / ordinary names      | Unified: no tag↔func/var clash                       | Homonyms allowed (POSIX `if_nameindex`, …); bare name prefers the function/var |

## Build System & Driver Integration

Dependencies are typically declared directly inside user source files:

```c
#pragma modc c_libs(m)
#pragma modc frameworks(OpenGL)
#pragma modc c_sources(shim.c)   // compile/link foreign C/ObjC/C++
```

To integrate external code, use `import` for pure %C packages while reserving `#include` paired with link pragmas for native C libraries and platform SDKs. Standard free functions without the `overload` modifier retain their original source symbol names to ensure seamless direct invocation from C, whereas object methods undergo name mangling using the `pkg_type_method` pattern.

## Hosted include stubs

`modc` ships curated headers under `lib/modc/include` (in-tree: `src/host/include`).
They are searched **before** host system include paths, so `#include <stdint.h>`,
`#include <unistd.h>`, or `#include <windows.h>` resolve to the stub, then link
against the real libc or `kernel32`.

The stubs are a **deliberate subset**: freestanding/hosted C (`stdio`, `stdlib`, …),
POSIX pieces needed by the `fs` / `os` packages (`unistd`, `fcntl`, `sys/stat`,
`dirent`, `poll`, `time`, `sys/wait`, `signal`), and a thin Win32 surface
(`windows.h` with the `*A` APIs those packages call). Prefer growing a stub
when a package needs a new call; use `#pragma modc c_sources(...)` for large
native code. Parsing full installed Windows/GTK SDK trees is **not** a goal—
stubs plus ordinary C headers are the supported surface.

The preprocessor supports the usual `#define` / `#include` / `#if` subset used by
most headers (including correct `|` / `^` / `&` and relational vs equality
precedence). Obscure or SDK-only preprocessor features are out of scope; extend
a stub instead of growing `pp.c`. `#pragma once` deduplicates includes; classic
#ifndef include-guard heuristics are not used as a skip cache.

Package logic lives in `.mc` sources. System API declarations belong in hosted
stubs (`unistd.h`, `windows.h`, …), not in the package.

During compilation, the driver automatically injects target platform macros such as `__APPLE__`, `_WIN32`, and relevant architecture flags, though it intentionally omits `__GNUC__` to suppress heavy attribute macros. When targeting Windows, keep in mind that the object file format (COFF/PE) and the calling convention (Microsoft x64 via QBE `amd64_win`) operate as distinct layers; linkers cannot automatically rewrite SysV call sequences into Win64 ABI calls.

## Const Across the Boundary

User `.mc` source and headers both spell `const` (and user source may use
`const?` for passthrough). Header `const T *` maps to the same
`Type.is_readonly` as user `const`. String literals are const. Host
`<string.h>` / `<stdlib.h>` search APIs (`strchr`, `strstr`, `memchr`,
`bsearch`, …) are declared with `const?` so one libc symbol keeps call-site
constness. See [const.md](const.md).
