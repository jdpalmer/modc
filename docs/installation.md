# Installation

This guide describes how to get a working `modc` distribution on supported hosts.  If you are looking for the documentation index, see [README](README.md).

---

## Packaged installation (TODO)

**Not shipped yet.** The preferred way to install `modc` will be through common packaging systems (Homebrew, apt, Chocolatey, …) or standalone installation archives.

When those exists, this section should cover:

- Download / package install
- Where `modc`, hosted headers (`lib/modc/include/`), and stdlib packages (`lib/modc/pkg/`) land
- Whether `qbe` is bundled or required separately
- `modc --version` / path checks

Until then, **build from source** per the instructions below.

---

## Prerequisites

`modc` requires a host C compiler both to build `modc` itself and also to assemble and link `modc build` output. `clang` and `gcc` are supported on Posix-like systems (e.g., MacOS, Linux), while Visual Studio with `clang` is required on Windows.

---

## MacOS / Linux / Posix

You can build `modc` from a clone of this repository using standard build tools:

```sh
make          # → ./modc
make check    # optional: full test corpus
```

Which then can be installed into either the default location (`/usr/local`) or your own `$PREFIX`.

```sh
make install
# PREFIX=/opt/modc make install
```

`modc` uses the following layout for installed files:

```sh
$PREFIX/bin/modc
$PREFIX/lib/modc/include/    # hosted include stubs
$PREFIX/lib/modc/pkg/        # shipped packages
```

If you are hacking on `modc` itself, the in-tree `./modc` uses the repo’s `src/host/include` and package dirs. The installed binary finds headers/packages relative to its location (or `MODC_INCLUDE` / `MODC_PKG`).

---

## Windows (VS Clang + nmake)

On Windows you will need **Visual Studio** with Desktop C++ and the **Clang/LLVM** component. You will need `qbe` compiled for Windows and installed in your `PATH`.

With these preequisites installed, open an **x64 Native Tools** / Developer shell and build with:

```bat
nmake /f Makefile.msvc
```

TODO: There is no `nmake` installation rule yet.

TODO: MSYS2 / Git Bash may work with the `Makefile` but this is untested.

---

## Hello world.

Once installed, the typical way to build a project is:

```sh
cd helloworld
modc build
```

For a more complete guide on getting started with %C, please refer to [`quickstart`](quickstart.md).
