FROM gitlab-registry.cern.ch/adaptiveperf/gentoo-fp:latest
RUN mkdir -p /root/adaptiveperf
COPY . /root/adaptiveperf/
RUN MAKEOPTS="-j8" USE="-extra" emerge --quiet-build=y sys-devel/clang && cd /root/adaptiveperf && ./build.sh && { echo | PATH=$PATH:$(dirname /usr/lib/llvm/*/bin/clang) ./install.sh; } && cd .. && rm -rf adaptiveperf && emerge --deselect sys-devel/clang sys-devel/llvm && emerge --depclean
RUN useradd -m gentoo-aperf
USER gentoo-aperf
