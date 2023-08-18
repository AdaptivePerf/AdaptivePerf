# TestingInfra
A set of Dockerfiles for setting up the Docker-based testing infrastructure for SYCLOPS components developed at CERN.

## Contents
* **OpenSYCL:** Open SYCL + LLVM + OpenMP + CUDA + perf (based on AlmaLinux 9)
* **SYCL-ROOT**: dependencies needed for ROOT with SYCL extensions (based on the OpenSYCL image)

Open SYCL, OpenMP, and Python are compiled with `-g`, `-fno-omit-frame-pointer` and `-mno-omit-leaf-frame-pointer` flags to facilitate profiling.

All Dockerfiles have setup parameters that can be customised by the user. It should be noted that Python 3.12 introduces [perf support](https://docs.python.org/3.12/howto/perf_profiling.html).
