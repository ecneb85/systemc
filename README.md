# SystemC TLM-2.0: AT four-phase protocol

[![CI](https://github.com/ecneb85/systemc/actions/workflows/ci.yml/badge.svg)](https://github.com/ecneb85/systemc/actions/workflows/ci.yml)

A small, runnable learning project for understanding the SystemC TLM-2.0
architecture, with emphasis on the approximately timed (AT) coding style. A
CPU initiator writes one byte to a memory target and reads it back using the
non-blocking four-phase protocol.

GitHub Actions builds and runs the simulation with both GCC and Clang on
Ubuntu, and checks the four-phase order and expected timestamps for both
transactions. See [`.github/workflows/ci.yml`](.github/workflows/ci.yml).

## Features

- TLM-2.0 initiator and target sockets
- Non-blocking `nb_transport_fw` and `nb_transport_bw` callbacks
- `BEGIN_REQ`, `END_REQ`, `BEGIN_RESP`, and `END_RESP` phase trace
- Annotated request, response, and end-response timing
- CMake project that can be built with GCC or Clang
- VS Code CMake build and simulation tasks

## Quick start

### Requirements

- CMake 3.16 or newer
- A C++17 compiler: GCC or Clang (C++20 is selected automatically for the
  MSYS2 UCRT64 SystemC package)
- An installed SystemC library with TLM-2.0 headers

Clone the repository, configure it, build, and run:

```sh
git clone https://github.com/ecneb85/systemc.git
cd systemc
cmake -S . -B build -DCMAKE_CXX_COMPILER=g++
cmake --build build
./build/tlm_at_four_phase
```

Use `clang++` instead of `g++` to select Clang. On Windows, run
`.\build\tlm_at_four_phase.exe` from PowerShell. The MSYS2 UCRT64 setup below
has additional SystemC package-specific instructions.

In VS Code, open the project folder, configure CMake with your compiler, then
run **Terminal: Run Task → SystemC: simulate**. The simulation task builds the
selected CMake launch target before running it.

## What to study

Start with `src/main.cpp` and trace a transaction through `Cpu::transact`,
`ByteMemory::nb_transport_fw`, and both event-queue callbacks. The CPU's
initiator socket is bound to the memory's target socket in `sc_main`; the
generic payload carries the command, address, byte buffer, and response status.
Both sides use `peq_with_cb_and_phase` to defer work without blocking inside a
transport callback.

### LT and AT at a glance

| Style | Typical transport | How timing is modeled |
| --- | --- | --- |
| Loosely timed (LT) | Blocking `b_transport` | Annotated delay; fewer timing details and phases. |
| Approximately timed (AT) | Non-blocking `nb_transport_fw` / `nb_transport_bw` | Request and response phases can be observed and scheduled independently. |

This project focuses on AT, where the phase transitions make arbitration and
concurrency visible. It is still an abstraction: the example models transaction
latency, not individual clock cycles or signal toggles.

## The four phases

| Phase | Direction | Meaning in this example |
| --- | --- | --- |
| `BEGIN_REQ` | CPU → memory | CPU starts a request; target accepts it and returns `TLM_ACCEPTED`. |
| `END_REQ` | memory → CPU | Target ends the request phase after 5 ns. The request channel is released. |
| `BEGIN_RESP` | memory → CPU | Target presents the completed read/write result 10 ns after `END_REQ`. |
| `END_RESP` | CPU → memory | Initiator ends the response phase 2 ns later; target returns `TLM_COMPLETED`. |

The direction changes because the target sends `END_REQ` and `BEGIN_RESP` with
backward transport, while the initiator sends `BEGIN_REQ` and `END_RESP` with
forward transport.

The corresponding AT transaction path is:

```text
Initiator                         Target
    | -- BEGIN_REQ ---------------> |
    | <-- END_REQ ----------------- |
    | <-- BEGIN_RESP ------------- |
    | -- END_RESP ----------------> |
```

For the first transaction, starting at 1 ns, the expected phase times are:

```text
 1 ns  BEGIN_REQ
 6 ns  END_REQ
16 ns  BEGIN_RESP
18 ns  END_RESP
```

`TLM_ACCEPTED` means that the receiver accepted the phase and will continue the
transaction asynchronously. It is not the same as transaction completion.
`TLM_UPDATED` (not used in this example) means the receiver changed the phase
and/or its timing annotation as part of the return. `TLM_COMPLETED` at
`END_RESP` tells the initiator the target is finished with the payload. The
initiator keeps both the payload and its data byte alive until then.
`peq_with_cb_and_phase` is used to model delayed callbacks without blocking
inside either non-blocking transport interface.

### Timing and transaction lifetime

The 5 ns request latency, 10 ns response latency, and 2 ns end-response delay
are annotations that schedule phase callbacks on the SystemC simulation
timeline; the transport callbacks themselves do not call `wait`. Once `BEGIN_REQ`
is accepted, the initiating thread waits on an event while PEQ callbacks move
the transaction forward. The payload and its byte buffer are local to
`Cpu::transact`, so that thread does not leave the function until the target
returns `TLM_COMPLETED`. Releasing or reusing either object earlier would leave
the target with invalid data.

The CPU issues one transaction at a time in this first exercise. A real
AT interconnect/target can accept overlapping transactions; before adding that
behavior, track each payload's state independently and respect the base
protocol's request/response exclusion rules. Good next exercises are allowing
the CPU to send another request after `END_REQ`, adding a second initiator, and
adding a custom payload extension. Change the latency constants and compare the
new schedule with the trace above.

SystemC is intentionally not downloaded or built by this project. Point CMake
at your local SystemC installation using its package config, `pkg-config`, or
the `SYSTEMC_INCLUDE_DIR` and `SYSTEMC_LIBRARY` cache variables.

## Build with GCC

```sh
cmake -S . -B build-gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build-gcc
./build-gcc/tlm_at_four_phase
```

## Build with Clang

```sh
cmake -S . -B build-clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-clang
./build-clang/tlm_at_four_phase
```

## Build with MSYS2 UCRT64 on Windows

Install the GCC toolchain, CMake, Ninja, and SystemC from an MSYS2 UCRT64
terminal:

```sh
pacman -Syu
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-systemc
```

Configure the project from PowerShell or VS Code using the UCRT64 GCC toolchain.
CMake detects the SystemC installation next to that compiler and creates the
missing import library inside the active build directory:

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=C:\msys64\ucrt64\bin\g++.exe
cmake --build build
.\build\tlm_at_four_phase.exe
```

SystemC 3.0.2 in this package was built with C++20, so CMake selects C++20 for
this configuration. Other installations use C++17 by default, matching the
Ubuntu SystemC package used by CI. CMake needs `C:\msys64\ucrt64\bin` on `PATH`
to find `gendef` and `dlltool`; keep it on `PATH` when launching the executable
so Windows can also locate the SystemC DLL.

## Other SystemC installations

If SystemC is installed outside standard search paths, select its CMake package
config with `-DSystemCLanguage_DIR=/path/to/lib/cmake/SystemCLanguage`, or
provide:

```sh
cmake -S . -B build \
  -DSYSTEMC_INCLUDE_DIR=/path/to/systemc/include \
  -DSYSTEMC_LIBRARY=/path/to/systemc/lib/libsystemc.so
```

For a Windows library, pass the corresponding `.lib` path. Ensure the runtime
can locate the SystemC shared library when launching the executable.

## Publish to GitHub

Build output and generated CMake files are excluded by `.gitignore`; source,
documentation, formatting/lint configuration, and VS Code tasks remain
trackable. This project is prepared for `https://github.com/ecneb85/systemc`.
Create a feature branch, commit the changes, and push it:

```sh
git switch -c feature/tlm-at-learning-project
git add .
git commit -m "Add SystemC TLM-2.0 AT learning project"
git push -u origin feature/tlm-at-learning-project
```

Then open a pull request from `feature/tlm-at-learning-project` into `main`.
Do not add a license unless you have chosen one for this project.
