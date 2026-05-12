# Cairo — Athens → NVPTX (GPU) backend

Cairo compiles Athens source files to PTX (NVIDIA GPU assembly) using
LLVM's NVPTX target backend.

Functions named `kernel_*` in Athens source become CUDA kernel entries
when compiled. All other functions are compiled as device functions.

### Built-ins (for GPU stuff)

| `cairoThreadX()`   | threadIdx.x (as double)             |
| `cairoBlockX()`    | blockIdx.x  (as double)             |
| `cairoDimX()`      | blockDim.x  (as double)             |
| `cairoLoad(p, i)`  | Load double from kernel arg `p[i]`  |
| `cairoStore(p, i, v)` | Store `v` to kernel arg `p[i]`  |

`cairoLoad` and `cairoStore` reference kernel pointer args by
index (`0`, `1`, `2`, ...). The element index is computed as a double
(typically from thread/block index arithmetic) and converted to i64
internally.

Kernel arguments are currently modeled as `double*` buffers in CUDA global
memory. By default Cairo emits LLVM IR or PTX. A tiny demo runner is available
with `make CUDA=1 cairo`.

### vec_add.ath

```ath
extern cairoThreadX()
extern cairoBlockX()
extern cairoDimX()

def kernel_vec_add(a b c)
  var i = cairoBlockX() * cairoDimX() + cairoThreadX() in
  cairoStore(2, i, cairoLoad(0, i) + cairoLoad(1, i))
```

Use:

    ./build/bin/cairo foo.ath                 # emit PTX
    ./build/bin/cairo --emit-llvm foo.ath     # emit LLVM IR
    ./build/bin/cairo --run foo.ath           # launch first kernel_* with demo buffers
    ./build/bin/cairo --run --run-size 1024 foo.ath

## Build

    make compile
    make cairo

For the CUDA demo runner:

    make CUDA=1 cairo
