FROM tizenrt/tizenrt:2.0.1-arm64-local
COPY libc6-amd64.deb libssl1.1.deb /tmp/rtl8730e-deps/
RUN dpkg-deb -x /tmp/rtl8730e-deps/libc6-amd64.deb / && dpkg-deb -x /tmp/rtl8730e-deps/libssl1.1.deb / && rm -r /tmp/rtl8730e-deps
