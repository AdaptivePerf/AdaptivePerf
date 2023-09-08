### Based on Fedora 38 (which includes frame pointers by default)
FROM fedora:38


### Compile flags for everything (here: do not omit frame pointers for easier stack trace profiling)
ARG COMPILE_FLAGS="-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer"


### Switch to root
USER root
WORKDIR /root


### Install dependencies for installing GCC and glibc from source
RUN dnf update -y && dnf install -y dnf-plugins-core gcc gcc-c++ git gmp gmp-devel mpfr mpfr-devel libmpc libmpc-devel flex \
    perl diffutils gem wget bison rsync patch rpm-build texinfo automake rpm rpm-devel elfutils-libelf elfutils-libelf-devel \
    elfutils elfutils-devel && gem install fpm


### Install GCC from source
### (two GCC installations are needed: an older one as the default option for CUDA compatibility reasons 
### and a newer one for compiling perf in inheriting images)

## Older GCC
ARG GCC_VERSION=11

RUN mkdir gcc gcc/build && cd gcc && git clone https://github.com/gcc-mirror/gcc && cd gcc && git checkout releases/gcc-${GCC_VERSION}
RUN cd gcc/build && ../gcc/configure --prefix=/usr --disable-multilib && make -j CFLAGS_FOR_TARGET="-g ${COMPILE_FLAGS}" CXXFLAGS_FOR_TARGET="-g ${COMPILE_FLAGS}"
RUN cd gcc/build && rpm -e --nodeps gcc gcc-c++ && make install && cd ../.. && export VER=$(gcc --version | awk '/gcc/ {print $3}') && \
    fpm -s empty -t rpm -n gcc -v $VER -a native && fpm -s empty -t rpm -n gcc-c++ -v $VER -a native && dnf install -y *.rpm && rm -r *.rpm && \
    echo "exclude=gcc gcc-c++" >> /etc/dnf/dnf.conf

## Newer GCC with annobin
ARG GCC_VERSION=13

RUN cd gcc/gcc && git checkout releases/gcc-${GCC_VERSION} && cd ../build && rm -rf * && \
    ../gcc/configure --prefix=/usr/local --disable-multilib --program-suffix="${GCC_VERSION}" --enable-languages=c,c++ && \
    make -j CFLAGS_FOR_TARGET="-g ${COMPILE_FLAGS}" CXXFLAGS_FOR_TARGET="-g ${COMPILE_FLAGS}"
RUN cd gcc/build && make install && cd ../.. && rm -r gcc
RUN export PLUGIN_PATH=$(compgen -G "/usr/local/lib/gcc/$(uname -m)*/${GCC_VERSION}*/plugin") && git clone git://sourceware.org/git/annobin.git && cd annobin && \
    ./configure --with-gcc-plugin-dir=${PLUGIN_PATH} CC=gcc${GCC_VERSION} CXX=g++${GCC_VERSION} && \
    make -j && make install && ln -s ${PLUGIN_PATH}/annobin.so ${PLUGIN_PATH}/gcc-annobin.so && cd .. && rm -r annobin


### Install other dependencies
RUN dnf install -y cmake which kmod pciutils zlib-devel openssl openssl-devel libffi libffi-devel ninja-build


### Install CUDA
ARG CUDA_VERSION=11.8

RUN export CUDA_PACKAGE=${CUDA_VERSION/./-} && dnf config-manager --add-repo https://developer.download.nvidia.com/compute/cuda/repos/fedora35/x86_64/cuda-fedora35.repo && \
    dnf install -y cuda-cudart-${CUDA_PACKAGE} cuda-compat-${CUDA_PACKAGE} cuda-command-line-tools-${CUDA_PACKAGE} cuda-libraries-devel-${CUDA_PACKAGE} \
    cuda-minimal-build-${CUDA_PACKAGE} cuda-cudart-devel-${CUDA_PACKAGE} cuda-nvprof-${CUDA_PACKAGE} cuda-nvml-devel-${CUDA_PACKAGE} libcublas-devel-${CUDA_PACKAGE} \
    libnpp-devel-${CUDA_PACKAGE}
#RUN wget -O setup_driver.sh ${DRIVER_URL} && sh setup_driver.sh --accept-license --ui=none --no-kernel-module --no-questions && rm setup_driver.sh
#RUN wget -O setup_cuda.sh ${CUDA_URL} && sh setup_cuda.sh --silent --toolkit && rm setup_cuda.sh
ENV NVIDIA_VISIBLE_DEVICES=all


### Install Python from source
ARG PYTHON_VERSION=3.12

RUN git clone https://github.com/python/cpython && cd cpython && git checkout ${PYTHON_VERSION} && \
    ./configure --enable-shared --enable-optimizations --prefix=/usr CFLAGS="-g ${COMPILE_FLAGS}" CPPFLAGS="-g ${COMPILE_FLAGS}" && make -j && \
    make altinstall && cd .. && rm -r cpython

# Temporary workaround for https://github.com/python/cpython/issues/108819
RUN ln -s /usr/lib64/python${PYTHON_VERSION}/lib-dynload /usr/lib/python${PYTHON_VERSION}/ || true


### Install Boost from source
RUN wget https://boostorg.jfrog.io/artifactory/main/release/1.82.0/source/boost_1_82_0.tar.gz && tar xf *.tar.gz && cd boost_1_82_0 && ./bootstrap.sh --prefix=/usr && \
    ./b2 install cflags="-g ${COMPILE_FLAGS}" cxxflags="-g ${COMPILE_FLAGS}" && cd .. && rm -r boost*


### Set up essential tooling and configure "sycl" user
RUN dnf install -y gdb procps nano sudo flamegraph flamegraph-stackcollapse-perf ethtool flex \
    iperf3 python3-netaddr ncurses-devel netperf python3-pip && \
    ln -s /usr/bin/python3 /usr/bin/python && useradd -m sycl && echo "sycl ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers
USER sycl
WORKDIR /home/sycl


### Install LLVM, clang, libomp from source
ARG LLVM_GIT_URL=https://github.com/root-project/llvm-project
ARG LLVM_GIT_BRANCH=ROOT-llvm13

RUN git clone -b ${LLVM_GIT_BRANCH} --single-branch ${LLVM_GIT_URL} && mkdir llvm-project/build llvm-install && \
    cd llvm-project/build && cmake ../llvm -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_CXX_STANDARD=17 -DCMAKE_C_FLAGS="${COMPILE_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${COMPILE_FLAGS}" -DLLVM_ENABLE_PROJECTS="clang;openmp" -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_INSTALL_PREFIX=/home/sycl/llvm-install && cmake --build . && cmake --install . && cd ../.. && \
    rm -r llvm-project

ENV LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:/home/sycl/llvm-install/lib"
ENV PATH="${PATH}:/home/sycl/llvm-install/bin"
ENV CMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}:/home/sycl/llvm-install"
ENV C_INCLUDE_PATH="${C_INCLUDE_PATH}:/home/sycl/llvm-install/include"
ENV CPLUS_INCLUDE_PATH="${CPLUS_INCLUDE_PATH}:/home/sycl/llvm-install/include"


### Install Open SYCL
RUN git clone https://github.com/OpenSYCL/OpenSYCL && mkdir OpenSYCL/build OpenSYCL/build_install && cd OpenSYCL/build && \
    cmake ../ -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_FLAGS="${COMPILE_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${COMPILE_FLAGS}" -DCMAKE_INSTALL_PREFIX=/home/sycl/OpenSYCL/build_install && \
    make -j && make install

ENV LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:/home/sycl/OpenSYCL/build_install/lib"
ENV PATH="${PATH}:/home/sycl/OpenSYCL/build_install/bin"
ENV CMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}:/home/sycl/OpenSYCL/build_install"


### Install bcc and bcc-tools from source (this includes offcputime)
RUN pip3 install pyroute2 && git clone https://github.com/iovisor/bcc
COPY clang_libs.cmake /home/sycl/bcc/cmake/
RUN export LDFLAGS="-Wl,--copy-dt-needed-entries" && mkdir bcc/build bcc/install && cd bcc/build && \
    cmake ../ -DCMAKE_INSTALL_PREFIX=/home/sycl/bcc/install -DENABLE_LLVM_SHARED=ON && make -j && make install && \
    mkdir -p /home/sycl/bcc/install/lib/python && \
    ln -s /home/sycl/bcc/install/lib/python*/site-packages/bcc* /home/sycl/bcc/install/lib/python/bcc
COPY offcputime /home/sycl/bcc/install/share/bcc/tools/

ENV PYTHONPATH="${PYTHONPATH}:/home/sycl/bcc/install/lib/python/bcc"
ENV LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:/home/sycl/bcc/install/lib64"
ENV PATH="${PATH}:/home/sycl/bcc/install/share/bcc/tools"


### Set gdb to download debuginfo automatically and install profiling scripts
### and modified flamegraph.pl + stackcollapse-perf.pl
RUN pip3 install joblib && mkdir /home/sycl/benchmark && echo "set debuginfod enabled on" >> /home/sycl/.gdbinit
COPY get_addr get_name match_addr merge profile replace_addr split_ids split_report /home/sycl/benchmark/
COPY flamegraph.pl stackcollapse-perf.pl /usr/bin/
