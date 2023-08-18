ARG TAG=root-llvm
FROM gitlab-registry.cern.ch/syclops/testinginfra/opensycl:${TAG}


### Install dependencies required for building ROOT
RUN sudo dnf install -y yum-utils && sudo rpm -Uvh --nodeps $(repoquery --location libtool) && \
    sudo dnf install -y xrootd xrootd-devel \
    xrootd-client xrootd-client-devel ninja-build freetype freetype-devel \
    lz4 lz4-devel xz xz-devel tbb tbb-devel xxhash xxhash-devel libzstd \
    libzstd-devel libX11 libX11-devel Xorg \
    xorg-x11-server-devel libXpm libXpm-devel libXft libXft-devel \
    libXext libXext-devel
