# Edinburgh

Edinburgh is a tiny LLVM MIR scheduling playground. Instead, it plugs an out-of-tree scheduler into LLVM's existing machine scheduler. Here we also compares the MIR produced by LLVM's default scheduler against some policies we try out.

The current policy is small: it reuses LLVM's generic scheduler and adds a critical-path bias when choosing between ready instructions. That keeps things easy to inspect before adding heavier latency hiding or register-pressure heuristics.

Edinburgh uses three useful `llc` hooks:

`-stop-after=machine-scheduler` to inspect the MIR.

`-load ./build/lib/libedinburgh_sched.so` loads the out-of-tree shared library.

`-misched=edinburgh` selects the scheduler registered by that library through LLVM's `MachineSchedRegistry`.

## Build

Need LLVM/Clang 20, then build:

```bash
make -C langs/edinburgh
```

The plugin is written to:

```text
build/lib/libedinburgh_sched.so
```

## Test

```bash
make -C langs/edinburgh test
```

You should see:

```text
Edinburgh scheduler selected
```

The MIR files land under `build/edinburgh/`.

## Compare MIR

Compare LLVM's default scheduler against Edinburgh on the included LLVM IR input:

```bash
make -C langs/edinburgh compare
```

For `examples/tiny.ll`, the difference looks like this:

LLVM default:

```yaml
body:             |
  bb.0.entry:
    liveins: $rdi, $rsi, $rdx

    %2:gr64_nosp = COPY $rdx
    %3:gr64 = COPY $rsi
    %0:gr64 = COPY $rdi
    %3:gr64 = IMUL64rr %3, %0, implicit-def dead $eflags
    %5:gr64 = LEA64r %3, 1, %2, 0, $noreg
    %5:gr64 = XOR64rr %5, %0, implicit-def dead $eflags
    $rax = COPY %5
    RET 0, killed $rax
```

Edinburgh:

```yaml
body:             |
  bb.0.entry:
    liveins: $rdi, $rsi, $rdx

    %3:gr64 = COPY $rsi
    %0:gr64 = COPY $rdi
    %3:gr64 = IMUL64rr %3, %0, implicit-def dead $eflags
    %2:gr64_nosp = COPY $rdx
    %5:gr64 = LEA64r %3, 1, %2, 0, $noreg
    %5:gr64 = XOR64rr %5, %0, implicit-def dead $eflags
    $rax = COPY %5
    RET 0, killed $rax
```

Basically Edinburgh added a local choice here, prefer to schedule the instructions that have more work that depend on them. So the `%3` and `%0` are scheduled earlier than `%2`. This has the effect of moving the copy of the destination variable (`COPY %rdx`) later when it's needed. That's a cheap operation (unlike load or multiply) so LLVM's default scheduler didn't care too much while balancing a lot of other things.

---

Use a custom LLVM IR file with `INPUT`:

```bash
make -C langs/edinburgh compare INPUT=/path/to/program.ll
```

Athens can produce LLVM IR that Edinburgh can consume:

```bash
mkdir -p build/edinburgh
./build/bin/athens --llvmir test-programs/fib.ath > build/edinburgh/fib.ll
make -C langs/edinburgh compare INPUT=../../build/edinburgh/fib.ll
```

## Files

`src/edinburgh_sched.cpp` registers the scheduler and contains the policy.

`examples/tiny.ll` is a minimal LLVM IR input for smoke tests.

`scripts/compare-mir.sh` emits default and Edinburgh MIR, then runs an informal textual diff.
