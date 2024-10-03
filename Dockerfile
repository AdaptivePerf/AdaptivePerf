FROM gitlab-registry.cern.ch/adaptiveperf/gentoo-fp:latest
RUN emerge --quiet-build=y git && useradd -m gentoo-aperf && mkdir -p /root/adaptiveperf
COPY . /root/adaptiveperf/
RUN cd /root/adaptiveperf && ./build.sh && { echo | ./install.sh; } && cd .. && rm -rf adaptiveperf
USER gentoo-aperf
