FROM gitlab-registry.cern.ch/adaptiveperf/gentoo-fp:latest
RUN mkdir -p /root/adaptiveperf
COPY . /root/adaptiveperf/
ARG JOBS=1
RUN MAKEOPTS="-j${JOBS}" USE="-extra" emerge --quiet-build=y sys-devel/clang && cd /root/adaptiveperf && ./build.sh && { echo | PATH=$PATH:$(dirname /usr/lib/llvm/*/bin/clang) ./install.sh; } && cd .. && rm -rf adaptiveperf && emerge --deselect sys-devel/clang sys-devel/llvm && emerge --depclean
RUN setcap cap_perfmon,cap_bpf+ep /opt/adaptiveperf/perf/bin/perf && useradd -m gentoo-aperf
USER gentoo-aperf
