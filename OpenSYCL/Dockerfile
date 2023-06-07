# Based on AlmaLinux 9
FROM almalinux:9

# Setup parameters
ARG CUDA_VERSION=11.8
ARG LLVM_VERSION=15
ARG PYTHON_VERSION=3.12
ARG COMPILE_FLAGS="-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer"

# Switch to root
USER root
WORKDIR /root

# Install dependencies
RUN dnf update -y && dnf install -y gcc cmake git boost boost-devel dnf-plugins-core epel-release wget which kmod pciutils perl zlib-devel \
    openssl openssl-devel libffi libffi-devel

# Install CUDA
RUN export CUDA_PACKAGE=${CUDA_VERSION/./-} && dnf config-manager --add-repo http://developer.download.nvidia.com/compute/cuda/repos/rhel9/x86_64/cuda-rhel9.repo && \
    dnf install -y cuda-cudart-${CUDA_PACKAGE} cuda-compat-${CUDA_PACKAGE} cuda-command-line-tools-${CUDA_PACKAGE} cuda-libraries-devel-${CUDA_PACKAGE} \
    cuda-minimal-build-${CUDA_PACKAGE} cuda-cudart-devel-${CUDA_PACKAGE} cuda-nvprof-${CUDA_PACKAGE} cuda-nvml-devel-${CUDA_PACKAGE} libcublas-devel-${CUDA_PACKAGE} \
    libnpp-devel-${CUDA_PACKAGE}
#RUN wget -O setup_driver.sh ${DRIVER_URL} && sh setup_driver.sh --accept-license --ui=none --no-kernel-module --no-questions && rm setup_driver.sh
#RUN wget -O setup_cuda.sh ${CUDA_URL} && sh setup_cuda.sh --silent --toolkit && rm setup_cuda.sh
ENV NVIDIA_VISIBLE_DEVICES=all

# Install Python from source
RUN git clone https://github.com/python/cpython && cd cpython && git checkout ${PYTHON_VERSION} && \
    ./configure --enable-shared --enable-optimizations --prefix=/usr CFLAGS="-g ${COMPILE_FLAGS}" CPPFLAGS="-g ${COMPILE_FLAGS}" && make -j && \
    make altinstall && cd .. && rm -r cpython

# Install clang, LLVM
RUN dnf config-manager --set-enabled crb && dnf update -y && dnf install -y clang-${LLVM_VERSION}* clang-devel-${LLVM_VERSION}* \
    llvm-${LLVM_VERSION}* llvm-devel-${LLVM_VERSION}*

## We have to force clang to use GCC 11 (installed by default) because GCC 12 does not currently like CUDA...
RUN rpm -e --nodeps gcc-toolset-12-binutils gcc-toolset-12-binutils-gold gcc-toolset-12-gcc gcc-toolset-12-gcc-c++ \
    gcc-toolset-12-libstdc++-devel gcc-toolset-12-runtime && rm -rf /opt/rh/gcc-toolset-12/* && \
    ln -s / /opt/rh/gcc-toolset-12/root

# Install libomp with debug info
RUN git clone https://github.com/llvm/llvm-project && cd llvm-project && git checkout release/${LLVM_VERSION}.x && cd openmp && \
    cmake . -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_FLAGS="${COMPILE_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${COMPILE_FLAGS}" && make && make install && cd ../.. && rm -r llvm-project

# Install Open SYCL
RUN git clone https://github.com/OpenSYCL/OpenSYCL && mkdir build && cd build && cmake ../OpenSYCL -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_FLAGS="${COMPILE_FLAGS}" -DCMAKE_CXX_FLAGS="${COMPILE_FLAGS}" && \
    make && make install && cd .. && rm -r OpenSYCL build

# Set up essential tooling
RUN dnf install -y emacs nano sudo perf flamegraph flamegraph-stackcollapse-perf

# Configure user
RUN useradd -m sycl && echo "sycl ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers
USER sycl
WORKDIR /home/sycl
