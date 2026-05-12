// Tiny CUDA driver wrapper for Cairo's demo --run mode.

#pragma once

#include <cstddef>
#include <string>

class CUDALauncher {
public:
  CUDALauncher();
  ~CUDALauncher();

  bool loadPTX(const std::string &ptxSource);
  bool runKernel(const std::string &name, unsigned argCount,
                 std::size_t elementCount);

private:
  [[maybe_unused]] void *module_{nullptr};
  [[maybe_unused]] void *context_{nullptr};
};
