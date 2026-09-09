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
| Qualifiers and Specifiers | const, volatile, restrict, register, inline rejected | Allowed                               |
| Integer Types             | Fixed-width types required (int64_t)                 | Host ABI long / long long permitted   |
| Declarations              | One declaration per statement                        | Multi-declarators allowed (int a, b;) |
| Conditional Assignment    | Requires explicit parenthesization ((x = ...))       | Standard C rules                      |
| Control Flow              | Mandatory braces for if, for, while                  | Optional braces                       |
| Switch Statements         | requires `break` or `fallthrough;`                   | Implicit fallthrough allowed          |
| Function Declarations     | Function prototypes rejected                         | Standard C prototypes allowed         |

## Build System & Driver Integration

Dependencies are typically declared directly inside user source files:

```c
#pragma modc c_libs(m)
#pragma modc frameworks(OpenGL)
#pragma modc c_sources(shim.c)   // compile/link foreign C/ObjC/C++
```

To integrate external code, use `import` for pure %C packages while reserving `#include` paired with link pragmas for native C libraries and platform SDKs. Standard free functions without the `overload` modifier retain their original source symbol names to ensure seamless direct invocation from C, whereas object methods undergo name mangling using the `pkg_type_method` pattern.

During compilation, the driver automatically injects target platform macros such as `__APPLE__`, `_WIN32`, and relevant architecture flags, though it intentionally omits `__GNUC__` to suppress heavy attribute macros. When targeting Windows, keep in mind that the object file format (COFF/PE) and the calling convention (Microsoft x64 via QBE `amd64_win`) operate as distinct layers; linkers cannot automatically rewrite SysV call sequences into Win64 ABI calls.

## Auto-const Across the Boundary

User `.mc` source code cannot spell the `const` keyword. When importing standard C headers, `const T *` parameters and return values are preserved internally as `READONLY` to ensure string literals and immutability guarantees remain safe across boundaries. See [auto-const.md](auto-const.md).
