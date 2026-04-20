# Assignment 3 Starter Code and Use Notes
-----------------------------------------------------------------------------------------------------
In order to run the tests on this program enter the following commands:
go to the directory with unifiedpass.cpp
- make
This will make unifiedpass.cpp
- make tests
This will run all the tests and print the output for each pass in unifiedpass.cpp

The dominators pass will print the in sets and outsets for every BB of the converged program. It will also print all dominator relationships
The LICM code will print all of the hoistable instructions and all of the stats before (unoptimized) and after (optimized) code. 
There are a total of 4 LICM tests.
-----------------------------------------------------------------------------------------------------

This starter package contains:
- `unifiedpass.cpp`: LLVM plugin starter with
  - reusable fixed-point dataflow engine skeleton
  - set-print helper utilities
  - a partially wired Available Expressions template (with TODO transfer logic)
  - a map-based Constant Propagation starter using a 3-point lattice
    (`TOP`, `Const`, `NAC`) with TODO extension points
  - pass registration for
  - `available`
  - `liveness`
  - `reaching`
  - `constantprop`
  - `Dominators`
  - `Dead Code Elimination`
  - `LICM`
- `Makefile`: build + run targets
- `tests/`: 4 provided test inputs (`*.bc`)

## Build

```bash
make
```

This builds `build/unifiedpass.so`.

## Run all tests

```bash
make tests
```

This generates:
- `build/tests/*-m2r.ll` (disassembled inputs)
- `build/tests/*-opt.ll` (outputs after running each pass)

## Run one pass manually

```bash
opt -bugpoint-enable-legacy-pm=1 \
  -load-pass-plugin=build/unifiedpass.so \
  -passes='available' tests/available-test-m2r.bc -o /tmp/out.bc
```

Replace `available` with one of: `liveness`, `reaching`, `constantprop`.


