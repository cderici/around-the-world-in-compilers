# Around the World in Compilers

<!-- [![Build Status](https://github.com/cderici/around-the-world-in-26-languages/actions/workflows/athens.yml/badge.svg)](https://github.com/cderici/around-the-world-in-26-languages/actions/workflows/athens.yml) -->
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)
![LLVM 20](https://img.shields.io/badge/LLVM-20-orange.svg)

This is a collection of small experimental languages and compilers, one for every letter of the alphabet, each named after a world city. Here I explore the world of compilation, language design, compiler optimizations, and all the cool stuff around compiler technologies.

Some of these work on the frontend (e.g. `MLIR.smt` + `Z3` based verification), but most should focus on the backend (`LLVM` instruction selection and later passes), my interests generally lean towards the backend. 

They share pieces of compiler infrastructure whenever possible (e.g. lexing/parsing). Some target `LLVM IR`, some target `PTX` for CUDA experiments, some target `MLIR`, and some include a repl (e.g. Athens has an LLVM ORC-based JITted repl).

It's my personal lab to explore ideas in language design and implementation, IR construction, optimization behavior, and runtime. It's an open-ended playground, so it will probably never be fully complete.

<br>

There's no semantic relation between the cities and the languages, it's just fun naming.

<br>

| Languages | Concept / Focus | Based&nbsp;On | Status |
|----------|-----------------|--------------|---------|
| [Athens](https://github.com/cderici/around-the-world-in-26-languages/blob/main/langs/athens/README.md)   |  Enhanced Kaleidoscope-style language targeting LLVM IR, with an LLVM ORC JIT repl.  | -- | [![Athens CI](https://github.com/cderici/around-the-world-in-26-languages/actions/workflows/athens.yml/badge.svg)](https://github.com/cderici/around-the-world-in-26-languages/actions/workflows/athens.yml) |
| [Berlin](https://github.com/cderici/around-the-world-in-26-languages/blob/main/langs/berlin/README.md)   |  Produces MLIR in `affine`/`linalg` dialects for polyhedral analysis. Enables loop tiling, fusion, interchange, and dependence analysis before final lowering. | Athens | [![Berlin CI](https://github.com/cderici/around-the-world-in-26-languages/actions/workflows/berlin.yml/badge.svg)](https://github.com/cderici/around-the-world-in-26-languages/actions/workflows/berlin.yml) |
| [Cairo](https://github.com/cderici/around-the-world-in-26-languages/blob/main/langs/cairo/README.md)    |  This is basically Athens with LLVM IR targeting `nvptx64-nvidia-cuda`, producing `PTX` kernels via NVIDIA GPU codegen. Has an option to launch them via the CUDA driver API.  | Athens | [![Cairo CI](https://github.com/cderici/around-the-world-in-26-languages/actions/workflows/cairo.yml/badge.svg)](https://github.com/cderici/around-the-world-in-26-languages/actions/workflows/cairo.yml) |
| [Dublin](https://github.com/cderici/around-the-world-in-26-languages/blob/main/langs/dublin/README.md)   |  MLIR based verification playground. Takes a small `func`/`arith`/`cf.assert` fragment, uses the `smt` dialect for the assertion fail query, emits SMT LIB (`.smt2`), runs `Z3`, and reports verified or counterexample. Currently just straight line bitvector arithmetic, then we'll grow into control flow, loops, `affine`/`linalg`, shapes, etc. | Berlin / Athens | [![Dublin CI](https://github.com/cderici/around-the-world-in-26-languages/actions/workflows/dublin.yml/badge.svg)](https://github.com/cderici/around-the-world-in-26-languages/actions/workflows/dublin.yml) |
| [Edinburgh](https://github.com/cderici/around-the-world-in-26-languages/blob/main/langs/edinburgh/README.md)|  LLVM MIR scheduling playground. Implements custom MachineScheduler strategies that choose from LLVM's ready set using critical path, latency hiding, and register pressure heuristics, then compares the resulting MIR against LLVM's default scheduler. | Athens / Cairo | [![Edinburgh CI](https://github.com/cderici/around-the-world-in-26-languages/actions/workflows/edinburgh.yml/badge.svg)](https://github.com/cderici/around-the-world-in-26-languages/actions/workflows/edinburgh.yml) |
| Florence | A shot at *architecture-aware* design for large-scale systems. Trying to make architectural considerations explicit and enforcable: first-class modules, boundaries, layers, dependency directions, etc, for clean separation, stable interfaces, and explicit/intentional (de-)coupling. | -- | <small>Planning</small>|
| Geneva | Uses Tapir/LLVM + `Cilk` (Plus) runtime system to explore paralellism (e.g. loop parallelism via `cilk_for`). Compares LLVM IR structure and performance against Berlin in various architectures.  | -- | <small>Planning</small> |
| Havana   |                 |
| Istanbul |                 |
| Jakarta  |                 |
| Kyoto    |                 |
| Lisbon   |                 |
| Montreal |                 |
| Nairobi  |                 |
| Oslo     |                 |
| Prague   |                 |
| Quito    |                 |
| Reykjavík|                 |
| Seoul    |                 |
| Tallinn  |                 |
| Utrecht  |                 |
| Vienna   |                 |
| Warsaw   |                 |
| Xanthus  |                 |
| Yerevan  |                 |
| Zürich   |                 |
