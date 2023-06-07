FROM opensycl

# Setup parameters
ARG ROOT_GIT=https://github.com/maksgraczyk/root
ARG ROOT_BRANCH=invariantmass_gpu
ARG COMPILE_FLAGS="-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer"
ARG PYTHON_VERSION=3.12

# Install dependencies
RUN sudo dnf install -y xrootd xrootd-devel \
    xrootd-client xrootd-client-devel ninja-build freetype freetype-devel \
    lz4 lz4-devel xz xz-devel tbb tbb-devel xxhash xxhash-devel libzstd \
    libzstd-devel libX11 libX11-devel Xorg \
    xorg-x11-server-devel libXpm libXpm-devel libXft libXft-devel \
    libXext libXext-devel

# Clone and compile ROOT
RUN mkdir root && cd root && mkdir build && git clone ${ROOT_GIT}
RUN cd root/root && git checkout ${ROOT_BRANCH} && cd ../build && \
    cmake ../root -G Ninja -DCMAKE_CXX_STANDARD=17 -Dcuda=ON \
    -Dsycl=ON -DOPENSYCL_TARGETS="omp;cuda:sm_70" \
    -DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc \
    -DCMAKE_CUDA_ARCHITECTURES=70 -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_FLAGS="${COMPILE_FLAGS}" -DCMAKE_CXX_FLAGS="${COMPILE_FLAGS}" \
    -DPYTHON_EXECUTABLE=/usr/bin/python${PYTHON_VERSION} \
    -DPYTHON_INCLUDE_DIR=/usr/include/python${PYTHON_VERSION} \
    -DPYTHON_LIBRARY=/usr/lib/libpython${PYTHON_VERSION}.so && cmake --build .

# Finish
RUN echo "source /home/sycl/root/build/bin/thisroot.sh" >> /home/sycl/.bashrc
