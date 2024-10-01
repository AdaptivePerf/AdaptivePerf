FROM gentoo/stage3:latest
RUN sed -i -E -e 's/COMMON_FLAGS\="(.+)"/COMMON_FLAGS="\1 -g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer"/g' /etc/portage/make.conf && echo "FEATURES=\"nostrip\"" >> /etc/portage/make.conf
RUN emerge --sync && emerge --emptytree @world
RUN emerge dev-cpp/cli11 dev-cpp/nlohmann_json sys-devel/clang dev-libs/boost dev-libs/poco sys-process/numactl
RUN useradd -m gentoo-aperf && mkdir -p /root/adaptiveperf
COPY . /root/adaptiveperf/
RUN cd /root/adaptiveperf && ./build.sh && { echo | ./install.sh } && cd .. && rm -rf adaptiveperf
USER gentoo-aperf
