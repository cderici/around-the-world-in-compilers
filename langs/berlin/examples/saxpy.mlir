module {
  func.func private @saxpy(%arg0: f64, %arg1: f64, %arg2: f64) -> f64 {
    %alloca = memref.alloca() : memref<f64>
    memref.store %arg0, %alloca[] : memref<f64>
    %alloca_0 = memref.alloca() : memref<f64>
    memref.store %arg1, %alloca_0[] : memref<f64>
    %alloca_1 = memref.alloca() : memref<f64>
    memref.store %arg2, %alloca_1[] : memref<f64>
    %0 = memref.load %alloca[] : memref<f64>
    %1 = memref.load %alloca_0[] : memref<f64>
    %2 = arith.mulf %0, %1 : f64
    %3 = memref.load %alloca_1[] : memref<f64>
    %4 = arith.addf %2, %3 : f64
    return %4 : f64
  }
}

