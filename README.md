ElevenSeventyFive Core (1175 / ESF)
===================================

ElevenSeventyFive (ticker **ESF**, chain "1175") is a SHA-256 proof-of-work
cryptocurrency. It supports **AuxPoW merged mining**, so it can be mined
alongside other SHA-256 chains, and uses the **ASERT** difficulty algorithm.
This repository is the ElevenSeventyFive Core node, wallet and GUI, built on
Bitcoin Core.

Downloads
---------

Pre-built binaries for Linux, Windows and macOS are on the
[releases page](https://github.com/1175Dev/1175/releases). Verify your download
against the `SHA256SUMS` published with each release. You can also build from
source (see below).

Upgrade notice — consensus fork at block 31733
----------------------------------------------

**v29.0.0 activates AuxPoW and ASERT at block 31733.** Every node operator must
be running v29 before that height; nodes left on v25.1 will not follow the chain
past the fork. See the v29.0.0 release notes for details and verification steps.

What is ElevenSeventyFive Core?
-------------------------------

ElevenSeventyFive Core connects to the ElevenSeventyFive peer-to-peer network to
download and fully validate blocks and transactions. It also includes a wallet
and graphical user interface, which can be optionally built.

License
-------

ElevenSeventyFive Core is released under the terms of the MIT license. See
[COPYING](COPYING) for more information or see
https://opensource.org/licenses/MIT.

Building from source
--------------------

ElevenSeventyFive Core builds **all** of its library dependencies (Boost,
libevent, SQLite, Berkeley DB, Qt, ...) from source with its own
[depends system](/depends). This keeps builds reproducible, so you do **not**
install those libraries on your machine — only a small base toolchain. After a
build, the binaries are in `build/bin/`: `elevenseventyfived` (daemon),
`elevenseventyfive-qt` (GUI wallet), `elevenseventyfive-cli`, `-tx`, `-util` and
`-wallet`.

### 1. Base toolchain

Only a compiler and build tools are needed — the libraries come from depends.

Debian / Ubuntu:

```sh
sudo apt update
sudo apt install build-essential cmake pkgconf python3 curl bison
```

macOS (Xcode command line tools + [Homebrew](https://brew.sh)):

```sh
xcode-select --install
brew install cmake pkgconf
```

For a Windows (cross) build, also add the mingw-w64 toolchain and select the
POSIX threading variant (Core requires it):

```sh
sudo apt install g++-mingw-w64-x86-64-posix
sudo update-alternatives --config x86_64-w64-mingw32-g++    # choose the *-posix entry
```

### 2. Build the dependencies with depends

This compiles every library from source into `depends/<host>/` — reproducibly,
without touching your system:

```sh
make -C depends -j"$(nproc)"                              # Linux / macOS (native)
# or cross-compile for Windows:
#   make -C depends HOST=x86_64-w64-mingw32 -j"$(nproc)"
```

See [depends/README.md](/depends/README.md) for other HOST triplets and options
(e.g. `NO_QT=1`, `NO_WALLET=1`).

### 3. Build ElevenSeventyFive Core against depends

Point CMake at the toolchain that depends generated, then build:

```sh
cmake -B build --toolchain "$PWD/depends/$(depends/config.guess)/toolchain.cmake"
cmake --build build -j"$(nproc)"
```

For a Windows cross build, use the mingw toolchain instead:

```sh
cmake -B build --toolchain "$PWD/depends/x86_64-w64-mingw32/toolchain.cmake"
cmake --build build -j"$(nproc)"
```

The wallet (both descriptor/SQLite and legacy/Berkeley DB) and the GUI are built
when depends provides them, which it does by default.

Testing
-------

ElevenSeventyFive Core ships an extensive unit and functional test suite. Unit
tests build with the rest of the project and run with `ctest`; the Python
functional tests live in [/test](/test) and run with
`test/functional/test_runner.py`. Changes are expected to be covered by tests
and reviewed by someone other than the author, especially anything touching
consensus.
