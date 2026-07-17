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

Building
--------

The `master` branch is the current release series. Build instructions for each
platform are in the [doc](/doc) folder (`doc/build-unix.md`,
`doc/build-windows.md`, `doc/build-osx.md`). Dependencies can be built
reproducibly with the [depends system](/depends).

Testing
-------

ElevenSeventyFive Core ships an extensive unit and functional test suite. Unit
tests build with the rest of the project and run with `ctest`; the Python
functional tests live in [/test](/test) and run with
`test/functional/test_runner.py`. Changes are expected to be covered by tests
and reviewed by someone other than the author, especially anything touching
consensus.
