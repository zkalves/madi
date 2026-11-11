# Madí Containerfile (Docker & Podman compatible)
# See packaging/docker/Dockerfile for full annotations.
# ------------------------------------------------------------------------------
# Stage 1: Build & Test
# ------------------------------------------------------------------------------
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    ca-certificates \
    pkg-config \
    zlib1g-dev \
    libsqlite3-dev \
    qt6-base-dev \
    qt6-svg-dev \
    libgl1-mesa-dev \
    libegl1-mesa-dev \
    libxkbcommon-dev \
    librsvg2-bin \
    imagemagick \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .

RUN cmake -B build -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr

RUN cmake --build build -j$(nproc)
RUN ctest --test-dir build --output-on-failure
RUN DESTDIR=/staging cmake --install build

# ------------------------------------------------------------------------------
# Stage 2: Runtime
# ------------------------------------------------------------------------------
FROM ubuntu:24.04 AS runtime

LABEL org.opencontainers.image.title="Madí" \
      org.opencontainers.image.description="Model, Align & Diagram Interconnects" \
      org.opencontainers.image.licenses="GPL-3.0-or-later"

ENV DEBIAN_FRONTEND=noninteractive \
    QT_QPA_PLATFORM=xcb \
    XDG_RUNTIME_DIR=/tmp/runtime-madi

RUN apt-get update && apt-get install -y --no-install-recommends \
    libqt6core6 \
    libqt6gui6 \
    libqt6widgets6 \
    libqt6openglwidgets6 \
    libqt6svg6 \
    libqt6sql6 \
    libgl1-mesa-dri \
    libglx-mesa0 \
    libegl1 \
    libxkbcommon0 \
    libx11-xcb1 \
    libxcb-cursor0 \
    libxcb-icccm4 \
    libxcb-image0 \
    libxcb-keysyms1 \
    libxcb-randr0 \
    libxcb-render-util0 \
    libxcb-shape0 \
    libxcb-sync1 \
    libxcb-xfixes0 \
    libxcb-xinerama0 \
    libxcb-xkb1 \
    zlib1g \
    fonts-dejavu-core \
    fonts-noto-color-emoji \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /staging /
COPY packaging/docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

RUN useradd -m -s /bin/bash -u 1000 madi && \
    mkdir -p /tmp/runtime-madi && \
    chown -R madi:madi /tmp/runtime-madi

USER madi
WORKDIR /home/madi

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["madi-gui"]
