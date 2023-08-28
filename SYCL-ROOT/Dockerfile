ARG TAG=root-llvm
FROM gitlab-registry.cern.ch/syclops/testinginfra/opensycl:${TAG}


### Install dependencies required for building ROOT
RUN sudo dnf install -y yum-utils && sudo rpm -Uvh --nodeps $(repoquery --location libtool) && \
    sudo dnf install -y xz xz-devel libX11 libX11-devel Xorg \
    xorg-x11-server-devel libXpm libXpm-devel libXft libXft-devel \
    libXext libXext-devel
