FROM opensycl:cling-llvm

# Setup parameters
ARG CLING_GIT=https://github.com/maksgraczyk/cling
ARG CLING_BRANCH=ext_llvm_clang_fix
ARG COMPILE_FLAGS="-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer"

# Install dependencies
#RUN sudo dnf install -y xrootd xrootd-devel \
#    xrootd-client xrootd-client-devel ninja-build freetype freetype-devel \
#    lz4 lz4-devel xz xz-devel tbb tbb-devel xxhash xxhash-devel libzstd \
#    libzstd-devel libX11 libX11-devel Xorg \
#    xorg-x11-server-devel libXpm libXpm-devel libXft libXft-devel \
#    libXext libXext-devel

# Clone and compile cling
RUN git clone -b ${CLING_BRANCH} --single-branch ${CLING_GIT} && mkdir cling/build cling/install && \
    cd cling/build && export LDFLAGS="-Wl,--copy-dt-needed-entries" && \
    cmake ../ -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_FLAGS="${COMPILE_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${COMPILE_FLAGS}" -DCLING_INCLUDE_TESTS=Off -Dbuiltin_llvm=Off -Dbuiltin_clang=Off \
    -DBUILD_SHARED_LIBS=On -DCMAKE_INSTALL_PREFIX=/home/sycl/cling/install && \
    cmake --build . && cmake --install .

# Finish
ENV PATH="${PATH}:/home/sycl/cling/install/bin"
#RUN echo "source /home/sycl/root/build/bin/thisroot.sh" >> /home/sycl/.bashrc
