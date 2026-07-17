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

ElevenSeventyFive Core uses CMake. After a build, the binaries are in
`build/bin/`: `elevenseventyfived` (daemon), `elevenseventyfive-qt` (GUI wallet),
`elevenseventyfive-cli`, `-tx`, `-util` and `-wallet`. More detailed per-platform
notes are in the [doc](/doc) folder.

### Linux (Debian / Ubuntu)

```sh
sudo apt update
sudo apt install build-essential cmake pkgconf python3 libevent-dev libboost-dev

# optional: descriptor wallet (SQLite), GUI (Qt6) and QR support
sudo apt install libsqlite3-dev qt6-base-dev qt6-tools-dev libqrencode-dev

cmake -B build
cmake --build build -j"$(nproc)"
```

For legacy (Berkeley DB 4.8) wallet support and static / reproducible binaries,
build the dependencies first with the [depends system](/depends), then point the
build at its toolchain:

```sh
make -C depends -j"$(nproc)"
cmake -B build --toolchain "$PWD/depends/x86_64-pc-linux-gnu/toolchain.cmake"
cmake --build build -j"$(nproc)"
```

(Adjust `x86_64-pc-linux-gnu` to your host triple if you are not on 64-bit x86.)

### macOS

Install the Xcode command line tools and [Homebrew](https://brew.sh):

```sh
xcode-select --install
brew install cmake boost libevent pkgconf

# optional: descriptor wallet, GUI and QR support
brew install sqlite qt@6 qrencode

cmake -B build
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

### Windows

Windows binaries are cross-compiled from Linux (or WSL) with mingw-w64 and the
depends system. On Debian / Ubuntu:

```sh
sudo apt install build-essential cmake pkgconf python3 g++-mingw-w64-x86-64-posix
# select the POSIX threading variant if prompted (Core requires it):
sudo update-alternatives --config x86_64-w64-mingw32-g++    # choose the *-posix entry

# build all Windows dependencies (Boost, libevent, SQLite, Berkeley DB, Qt, ...)
make -C depends HOST=x86_64-w64-mingw32 -j"$(nproc)"

cmake -B build --toolchain "$PWD/depends/x86_64-w64-mingw32/toolchain.cmake"
cmake --build build -j"$(nproc)"
```

The resulting `.exe` files are in `build/bin/`.

Testing
-------

ElevenSeventyFive Core ships an extensive unit and functional test suite. Unit
tests build with the rest of the project and run with `ctest`; the Python
functional tests live in [/test](/test) and run with
`test/functional/test_runner.py`. Changes are expected to be covered by tests
and reviewed by someone other than the author, especially anything touching
consensus.
