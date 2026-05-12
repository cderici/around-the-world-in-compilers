module {
  func.func private @printd(f64) -> f64
  func.func private @loopTest() -> f64 {
    %cst = arith.constant 0.000000e+00 : f64
    %cst_0 = arith.constant 1.000000e+01 : f64
    %cst_1 = arith.constant 1.000000e+00 : f64
    %0 = arith.fptosi %cst : f64 to i64
    %1 = arith.fptosi %cst_0 : f64 to i64
    %2 = arith.fptosi %cst_1 : f64 to i64
    %3 = arith.index_cast %0 : i64 to index
    %4 = arith.index_cast %1 : i64 to index
    %5 = arith.index_cast %2 : i64 to index
    scf.for %arg0 = %3 to %4 step %5 {
      %6 = arith.index_cast %arg0 : index to i64
      %7 = arith.sitofp %6 : i64 to f64
      %alloca = memref.alloca() : memref<f64>
      memref.store %7, %alloca[] : memref<f64>
      %8 = memref.load %alloca[] : memref<f64>
      %9 = func.call @printd(%8) : (f64) -> f64
    }
    %cst_2 = arith.constant 0.000000e+00 : f64
    return %cst_2 : f64
  }
}

