# Dublin

Dublin is a tiny verification toy. You write a small `.dub` file with symbolic
bitvector inputs and `assert`s. Dublin turns each assertion into an MLIR `smt`
dialect query, exports SMT-LIB with `mlir-translate-21`, runs Z3, and says
whether the assertion is verified or has a counterexample.

Example:

```dub
// for every x, check that x + 0 == x
verify add_zero(x i32)
  assert x + 0 == x;
```

Build it from the repo root:

```sh
make dublin
```

Run an example:

```sh
./dublin langs/dublin/examples/verified.dub
```

For debugging, you can also see the generated MLIR `smt` dialect or SMT-LIB:

```sh
./dublin --emit-smt langs/dublin/examples/verified.dub
./dublin --emit-smt2 langs/dublin/examples/verified.dub
```
