# Berlin - Athens to MLIR

Berlin reuses the Athens lexer, parser, and AST, then lowers the parsed program
to MLIR. It emits standard MLIR dialects including `func`, `arith`, `memref`,
and `scf`.

## We have:

- Function definitions and extern prototypes.
- Numeric expressions using `f64` values.
- Variables, assignment, unary minus, and `var ... in` bindings.
- Binary operators: `+`, `-`, `*`, `<`, and `=`.
- `if then else` expressions lowered to `scf.if`.
- `for` expressions lowered to `scf.for`.
- Function calls lowered to `func.call`.

Loop bounds are converted from `f64` to integer/index values for `scf.for`.
When a loop omits the step expression, Berlin uses a default step of `1.0`.

## Example

```ath
extern printd(x);

def loopTest()
  for i = 0, 10, 1 in
    printd(i)
```

Running Berlin prints MLIR:

```sh
./build/bin/berlin langs/berlin/examples/simple_loop.ath
```

The output includes a private `func.func @loopTest` and an `scf.for` loop.

## Building

From the project root:

```sh
make berlin
```

From this directory:

```sh
make
```

## Examples

- `examples/simple_loop.ath`
- `examples/nested_loop.ath`
- `examples/saxpy.ath`
