FROM --platform=linux/amd64 ubuntu:16.04@sha256:a3785f78ab8547ae2710c89e627783cfa7ee7824d3468cae6835c9f4eae23ff7 AS elf2bin-glibc

RUN test "$(dpkg-query -W -f='${Version}' libc6:amd64)" = "2.23-0ubuntu11.3"

FROM --platform=linux/arm64 ubuntu:16.04@sha256:f4c51ba054967fd4b06715f1b67078efbe9ca152e8be98d8f3c1f4d08c6042f8

ARG DEBIAN_FRONTEND=noninteractive
ARG PYTHON_VERSION=3.8.18
ARG PYTHON_SHA256=7c5df68bab1be81a52dea0cc2e2705ea00553b67107a301188383d7b57320b16
ARG PROTOBUF_VERSION=3.5.0
ARG PROTOBUF_SHA256=8dd6b051c2b39ab95dfe1f53cc5e1662ceb852d856db57d85751696859698551
ARG GRPC_COMMIT=d45132a2e9246b11ddd0b70c07160076d5cbbb12
ARG TF_A_COMMIT=8ff55a9e14a23d7c7f89f52465bcc6307850aa33
ARG TF_A_SHA256=605ab51064398e161ba02fed0ac40b20021e1061a2c03c42583c1a3eeb1dfa16
ARG ARM_GCC_ARCHIVE=gcc-arm-none-eabi-10.3-2021.10-aarch64-linux.tar.bz2
ARG ARM_GCC_URL=https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/${ARM_GCC_ARCHIVE}
ARG ARM_GCC_SHA256=f605b5f23ca898e9b8b665be208510a54a6e9fdd0fa5bfc9592002f6e7431208

ENV PATH=/opt/gcc-arm-none-eabi-10.3-2021.10/bin:/usr/local/bin:${PATH}
ENV FIPTOOL=/usr/local/bin/fiptool

RUN set -eux; \
	printf '%s\n' \
		'deb http://ports.ubuntu.com/ubuntu-ports xenial main restricted universe multiverse' \
		'deb http://ports.ubuntu.com/ubuntu-ports xenial-updates main restricted universe multiverse' \
		'deb http://ports.ubuntu.com/ubuntu-ports xenial-security main restricted universe multiverse' \
		> /etc/apt/sources.list; \
	apt-get update; \
	apt-get install -y --no-install-recommends \
		build-essential \
		autoconf \
		automake \
		libtool \
		curl \
		make \
		g++ \
		unzip \
		cmake \
		python \
		python-pip \
		scons \
		git \
		genromfs \
		ca-certificates \
		bzip2 \
		file \
		pkg-config \
		libssl-dev \
		zlib1g-dev \
		libbz2-dev \
		libreadline-dev \
		libsqlite3-dev \
		libffi-dev \
		liblzma-dev \
		libncursesw5-dev \
		xz-utils; \
	rm -rf /var/lib/apt/lists/*

RUN set -eux; \
	curl -fsSL "${ARM_GCC_URL}" -o "/tmp/${ARM_GCC_ARCHIVE}"; \
	printf '%s  %s\n' "${ARM_GCC_SHA256}" "/tmp/${ARM_GCC_ARCHIVE}" | sha256sum -c -; \
	tar -xjf "/tmp/${ARM_GCC_ARCHIVE}" -C /opt; \
	rm "/tmp/${ARM_GCC_ARCHIVE}"; \
	arm-none-eabi-gcc --version

RUN set -eux; \
	curl -fsSL "https://www.python.org/ftp/python/${PYTHON_VERSION}/Python-${PYTHON_VERSION}.tgz" -o /tmp/python.tgz; \
	printf '%s  %s\n' "${PYTHON_SHA256}" /tmp/python.tgz | sha256sum -c -; \
	mkdir -p /tmp/python-src; \
	tar -xzf /tmp/python.tgz -C /tmp/python-src --strip-components=1; \
	cd /tmp/python-src; \
	./configure --prefix=/usr/local --enable-shared --with-ensurepip=install; \
	make -j"$(nproc)"; \
	make install; \
	ldconfig; \
	ln -sf /usr/local/bin/python3.8 /usr/local/bin/python3; \
	ln -sf /usr/local/bin/pip3.8 /usr/local/bin/pip3; \
	printf '%s\n' \
		'pip==25.0.1 --hash=sha256:c46efd13b6aa8279f33f2864459c8ce587ea6a1a59ee20de055868d8f7688f7f' \
		'setuptools==75.3.4 --hash=sha256:2dd50a7f42dddfa1d02a36f275dbe716f38ed250224f609d35fb60a09593d93e' \
		'wheel==0.45.1 --hash=sha256:708e7481cc80179af0e556bbf0cc00b8444c7321e2700b8d8580231d13017248' \
		'openpyxl==3.1.5 --hash=sha256:5282c12b107bffeef825f4617dc029afaf41d0ea60823bbb665ef3079dc79de2' \
		'et-xmlfile==2.0.0 --hash=sha256:7a91720bc756843502c3b7504c77b8fe44217c85c537d85037f0f536151b2caa' \
		> /tmp/python-requirements.txt; \
	python3 -m pip install --no-cache-dir --only-binary=:all: --require-hashes -r /tmp/python-requirements.txt; \
	rm -rf /tmp/python.tgz /tmp/python-src /tmp/python-requirements.txt

RUN set -eux; \
	curl -fsSL "https://github.com/protocolbuffers/protobuf/releases/download/v${PROTOBUF_VERSION}/protobuf-cpp-${PROTOBUF_VERSION}.tar.gz" -o /tmp/protobuf.tar.gz; \
	printf '%s  %s\n' "${PROTOBUF_SHA256}" /tmp/protobuf.tar.gz | sha256sum -c -; \
	mkdir -p /tmp/protobuf-src; \
	tar -xzf /tmp/protobuf.tar.gz -C /tmp/protobuf-src --strip-components=1; \
	cd /tmp/protobuf-src; \
	./configure --prefix=/usr/local; \
	make -j"$(nproc)"; \
	make install; \
	ldconfig; \
	rm -rf /tmp/protobuf.tar.gz /tmp/protobuf-src

RUN set -eux; \
	mkdir -p /tmp/grpc-src; \
	cd /tmp/grpc-src; \
	git init; \
	git remote add origin https://github.com/grpc/grpc.git; \
	git fetch --depth 1 origin "${GRPC_COMMIT}"; \
	git checkout --detach FETCH_HEAD; \
	test "$(git rev-parse HEAD)" = "${GRPC_COMMIT}"; \
	git rev-parse HEAD; \
	git submodule update --init --recursive; \
	git submodule status --recursive; \
	! git submodule status --recursive | grep -Eq '^[+-]'; \
	make -j"$(nproc)" grpc_cpp_plugin; \
	cp bins/opt/grpc_cpp_plugin /usr/local/bin/grpc_cpp_plugin; \
	rm -rf /tmp/grpc-src

RUN set -eux; \
	curl -fsSL "https://github.com/ARM-software/arm-trusted-firmware/archive/${TF_A_COMMIT}.tar.gz" -o /tmp/tf-a.tar.gz; \
	printf '%s  %s\n' "${TF_A_SHA256}" /tmp/tf-a.tar.gz | sha256sum -c -; \
	test "${TF_A_COMMIT}" = 8ff55a9e14a23d7c7f89f52465bcc6307850aa33; \
	mkdir -p /tmp/tf-a-src; \
	tar -xzf /tmp/tf-a.tar.gz -C /tmp/tf-a-src --strip-components=1; \
	cd /tmp/tf-a-src; \
	make fiptool BUILD_STRING="${TF_A_COMMIT}"; \
	install -m 0755 tools/fiptool/fiptool /usr/local/bin/fiptool; \
	file /usr/local/bin/fiptool | grep -q 'ARM aarch64'; \
	/usr/local/bin/fiptool version | grep -q 'v2.3'; \
	rm -rf /tmp/tf-a.tar.gz /tmp/tf-a-src

RUN set -eux; \
	test -x /opt/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-gcc; \
	python3 --version; \
	python3 -c 'from importlib.metadata import version; assert [version(name) for name in ("pip", "setuptools", "wheel", "openpyxl", "et-xmlfile")] == ["25.0.1", "75.3.4", "0.45.1", "3.1.5", "2.0.0"]'; \
	protoc --version; \
	fiptool version; \
	test -x /usr/local/bin/grpc_cpp_plugin; \
	file /usr/local/bin/grpc_cpp_plugin | grep -q 'ARM aarch64'

COPY --from=elf2bin-glibc \
	/lib/x86_64-linux-gnu/ld-2.23.so \
	/lib/x86_64-linux-gnu/libc-2.23.so \
	/lib/x86_64-linux-gnu/libm-2.23.so \
	/lib/x86_64-linux-gnu/

RUN set -eux; \
	mkdir -p /lib64; \
	ln -s /lib/x86_64-linux-gnu/ld-2.23.so /lib64/ld-linux-x86-64.so.2; \
	ln -s libc-2.23.so /lib/x86_64-linux-gnu/libc.so.6; \
	ln -s libm-2.23.so /lib/x86_64-linux-gnu/libm.so.6

WORKDIR /root/tizenrt/os
