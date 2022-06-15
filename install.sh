apt-get update \
	&& DEBIAN_FRONTEND="noninteractive" apt-get install --no-install-recommends -y \
	ca-certificates \
	cmake \
	gcc \
	g++ \
	git \
	libopus-dev \
	libsodium-dev \
	libvpx-dev \
	ninja-build \
	pkg-config \
	curl \
	tar
	
mkdir build
cd build
curl -JLO https://github.com/TokTok/c-toxcore/releases/download/v0.2.18/c-toxcore-0.2.18.tar.gz
tar -zxvf c-toxcore-0.2.18.tar.gz
rm c-toxcore-0.2.18.tar.gz
mv c-toxcore-0.2.18 c-toxcore
cmake -GNinja -B/build/c-toxcore/_build -H/build/c-toxcore \
	-DBOOTSTRAP_DAEMON=OFF \
	-DENABLE_STATIC=OFF \
	-DMUST_BUILD_TOXAV=ON \
	&& cmake --build /build/c-toxcore/_build --target install --parallel 4 \
	&& ldconfig

