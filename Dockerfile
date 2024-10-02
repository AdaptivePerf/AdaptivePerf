FROM gitlab-registry.cern.ch/adaptiveperf/gentoo-fp:master
RUN emerge --quiet-build=y dev-cpp/cli11 dev-cpp/nlohmann_json sys-devel/clang dev-libs/boost dev-libs/poco sys-process/numactl
RUN useradd -m gentoo-aperf && mkdir -p /root/adaptiveperf
COPY . /root/adaptiveperf/
RUN cd /root/adaptiveperf && ./build.sh && { echo | ./install.sh } && cd .. && rm -rf adaptiveperf
USER gentoo-aperf
