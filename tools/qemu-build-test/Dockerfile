FROM tizenrt/tizenrt:2.0.1-arm64-local
RUN apt-get update && apt-get install -y --no-install-recommends libglib2.0-dev libpixman-1-dev && rm -rf /var/lib/apt/lists/*
COPY qemu-2.12.0.tar.xz /tmp/qemu.tar.xz
COPY 16m.patch /tmp/16m.patch
RUN echo "e69301f361ff65bf5dabd8a19196aeaa5613c1b5ae1678f0823bdf50e7d5c6fc  /tmp/qemu.tar.xz" | sha256sum -c - && \
    mkdir -p /opt/qemu-src && tar -xJf /tmp/qemu.tar.xz -C /opt/qemu-src --strip-components=1 && \
    cd /opt/qemu-src && patch --fuzz=0 -p1 < /tmp/16m.patch && \
    ./configure --prefix=/opt/qemu-2.12.0-16m --target-list=arm-softmmu --disable-werror --disable-gtk --disable-sdl --disable-vnc --disable-curses --disable-docs && \
    make -j4 && make install && \
    /opt/qemu-2.12.0-16m/bin/qemu-system-arm --version && \
    rm -rf /opt/qemu-src /tmp/qemu.tar.xz /tmp/16m.patch
ENV PATH=/opt/qemu-2.12.0-16m/bin:${PATH}
