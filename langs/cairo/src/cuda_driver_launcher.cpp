#include "cuda_driver_launcher.h"

#include <algorithm>
#include <iostream>
#include <vector>

#ifdef HAVE_CUDA
#include <cuda.h>

static bool checkCuda(CUresult result, const char *what) {
  if (result == CUDA_SUCCESS)
    return true;

  const char *name = nullptr;
  const char *message = nullptr;
  cuGetErrorName(result, &name);
  cuGetErrorString(result, &message);
  std::cerr << "CUDA: " << what << " failed";
  if (name)
    std::cerr << " (" << name << ")";
  if (message)
    std::cerr << ": " << message;
  std::cerr << '\n';
  return false;
}
#endif

CUDALauncher::CUDALauncher() {
#ifdef HAVE_CUDA
  if (!checkCuda(cuInit(0), "cuInit"))
    return;

  CUdevice device;
  if (!checkCuda(cuDeviceGet(&device, 0), "cuDeviceGet"))
    return;

  CUcontext context;
  if (!checkCuda(cuCtxCreate(&context, 0, device), "cuCtxCreate"))
    return;

  context_ = context;
#endif
}

CUDALauncher::~CUDALauncher() {
#ifdef HAVE_CUDA
  if (module_)
    cuModuleUnload(static_cast<CUmodule>(module_));
  if (context_)
    cuCtxDestroy(static_cast<CUcontext>(context_));
#endif
}

bool CUDALauncher::loadPTX(const std::string &ptxSource) {
#ifdef HAVE_CUDA
  if (!context_)
    return false;

  CUmodule module;
  if (!checkCuda(cuModuleLoadData(&module, ptxSource.c_str()),
                 "cuModuleLoadData"))
    return false;

  module_ = module;
  return true;
#else
  (void)ptxSource;
  std::cerr << "Cairo was built without CUDA support. Rebuild with CUDA=1.\n";
  return false;
#endif
}

bool CUDALauncher::runKernel(const std::string &name, unsigned argCount,
                             std::size_t elementCount) {
#ifdef HAVE_CUDA
  if (!module_) {
    std::cerr << "CUDA: no PTX module loaded\n";
    return false;
  }

  CUfunction function;
  if (!checkCuda(cuModuleGetFunction(&function, static_cast<CUmodule>(module_),
                                     name.c_str()),
                 "cuModuleGetFunction"))
    return false;

  std::vector<std::vector<double>> host(argCount);
  std::vector<CUdeviceptr> device(argCount, 0);
  std::vector<void *> args(argCount);

  for (unsigned a = 0; a < argCount; ++a) {
    host[a].resize(elementCount);
    for (std::size_t i = 0; i < elementCount; ++i)
      host[a][i] = a + 1 == argCount ? 0.0 : static_cast<double>((a + 1) * i);

    std::size_t bytes = elementCount * sizeof(double);
    if (!checkCuda(cuMemAlloc(&device[a], bytes), "cuMemAlloc"))
      return false;
    if (!checkCuda(cuMemcpyHtoD(device[a], host[a].data(), bytes),
                   "cuMemcpyHtoD"))
      return false;
    args[a] = &device[a];
  }

  // Examples have no bounds checks, so launch exactly one thread per element.
  bool ok = checkCuda(cuLaunchKernel(function, static_cast<unsigned>(elementCount),
                                     1, 1, 1, 1, 1, 0, nullptr, args.data(),
                                     nullptr),
                      "cuLaunchKernel") &&
            checkCuda(cuCtxSynchronize(), "cuCtxSynchronize");

  if (ok) {
    for (unsigned a = 0; a < argCount; ++a) {
      std::size_t bytes = elementCount * sizeof(double);
      ok = checkCuda(cuMemcpyDtoH(host[a].data(), device[a], bytes),
                     "cuMemcpyDtoH") &&
           ok;
    }
  }

  for (CUdeviceptr ptr : device)
    if (ptr)
      cuMemFree(ptr);

  if (!ok)
    return false;

  unsigned out = argCount - 1;
  std::cout << "CUDA launch succeeded: " << name << '\n';
  std::cout << "arg" << out << ":";
  for (std::size_t i = 0; i < std::min<std::size_t>(elementCount, 8); ++i)
    std::cout << ' ' << host[out][i];
  std::cout << '\n';
  return true;
#else
  (void)name;
  (void)argCount;
  (void)elementCount;
  std::cerr << "Cairo was built without CUDA support. Rebuild with CUDA=1.\n";
  return false;
#endif
}
