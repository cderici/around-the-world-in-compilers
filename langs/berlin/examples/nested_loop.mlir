module {
  func.func private @printd(f64) -> f64
  func.func private @nestedLoops() -> f64 {
    %cst = arith.constant 0.000000e+00 : f64
    %cst_0 = arith.constant 5.000000e+00 : f64
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
      %cst_3 = arith.constant 0.000000e+00 : f64
      %cst_4 = arith.constant 5.000000e+00 : f64
      %cst_5 = arith.constant 1.000000e+00 : f64
      %8 = arith.fptosi %cst_3 : f64 to i64
      %9 = arith.fptosi %cst_4 : f64 to i64
      %10 = arith.fptosi %cst_5 : f64 to i64
      %11 = arith.index_cast %8 : i64 to index
      %12 = arith.index_cast %9 : i64 to index
      %13 = arith.index_cast %10 : i64 to index
      scf.for %arg1 = %11 to %12 step %13 {
        %14 = arith.index_cast %arg1 : index to i64
        %15 = arith.sitofp %14 : i64 to f64
        %alloca_7 = memref.alloca() : memref<f64>
        memref.store %15, %alloca_7[] : memref<f64>
        %16 = memref.load %alloca[] : memref<f64>
        %cst_8 = arith.constant 1.000000e+01 : f64
        %17 = arith.mulf %16, %cst_8 : f64
        %18 = memref.load %alloca_7[] : memref<f64>
        %19 = arith.addf %17, %18 : f64
        %20 = func.call @printd(%19) : (f64) -> f64
      }
      %cst_6 = arith.constant 0.000000e+00 : f64
    }
    %cst_2 = arith.constant 0.000000e+00 : f64
    return %cst_2 : f64
  }
}

