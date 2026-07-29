# SPDX-FileCopyrightText: 2026 CERN
# SPDX-License-Identifier: LGPL-3.0-or-later

FROM gitlab-registry.cern.ch/adaptyst/gentoo-fp:latest
RUN mkdir -p /root/adaptyst
COPY . /root/adaptyst/
RUN emerge --quiet-build=y media-libs/libpng
RUN emerge --quiet-build=y dev-build/ninja \
    dev-cpp/cli11 dev-cpp/nlohmann_json \
    dev-libs/boost app-arch/libarchive dev-libs/poco
RUN cd /root/adaptyst && \
    mkdir build && cd build && cmake .. -G Ninja && cmake --build . && \
    cmake --install . && cd ../.. && rm -rf adaptyst && ldconfig
RUN useradd -m gentoo-adaptyst
USER gentoo-adaptyst
