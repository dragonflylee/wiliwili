#!/bin/bash

set -e

export CMAKE_PREFIX_PATH=/opt/wiliwili
export PKG_CONFIG_PATH=$CMAKE_PREFIX_PATH/lib/pkgconfig
export LD_LIBRARY_PATH=$CMAKE_PREFIX_PATH/lib

wget -qO- https://curl.se/download/curl-8.7.1.tar.xz | tar Jxf - -C /tmp
wget -qO- https://downloads.videolan.org/pub/videolan/dav1d/1.5.0/dav1d-1.5.0.tar.xz | tar Jxf - -C /tmp
git clone https://code.videolan.org/videolan/libplacebo.git -b v6.338.2 --depth 1 --recurse-submodules /tmp/libplacebo
wget -qO- https://github.com/FFmpeg/nv-codec-headers/archive/n12.2.72.0.tar.gz | tar zxf - -C /tmp
wget -qO- https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz | tar Jxf - -C /tmp
wget -qO- https://github.com/mpv-player/mpv/archive/v0.39.0.tar.gz | tar zxf - -C /tmp

cd /opt/library/borealis/library/lib/extern/glfw
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$CMAKE_PREFIX_PATH \
  -DCMAKE_INSTALL_RPATH=$CMAKE_PREFIX_PATH/lib -DBUILD_SHARED_LIBS=ON -DGLFW_BUILD_WAYLAND=ON \
  -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF
cmake --build build
cmake --install build

cd /tmp/curl-8.7.1
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$CMAKE_PREFIX_PATH \
  -DCMAKE_INSTALL_RPATH=$CMAKE_PREFIX_PATH/lib -DBUILD_SHARED_LIBS=ON -DCURL_USE_OPENSSL=ON \
  -DHTTP_ONLY=ON -DCURL_DISABLE_PROGRESS_METER=ON -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF \
  -DUSE_LIBIDN2=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBPSL=OFF -DBUILD_LIBCURL_DOCS=OFF
cmake --build build
cmake --install build --strip

cd /tmp/dav1d-1.5.0
meson setup build --prefix=$CMAKE_PREFIX_PATH --libdir=lib --buildtype=release --default-library=shared \
  -Ddebug=false -Denable_tools=false -Denable_examples=false -Denable_tests=false -Denable_docs=false
meson compile -C build
meson install -C build

make PREFIX=$CMAKE_PREFIX_PATH -C /tmp/nv-codec-headers-n12.2.72.0 install

cd /tmp/ffmpeg-7.1
./configure --prefix=$CMAKE_PREFIX_PATH --enable-shared --disable-static \
  --ld=g++ --enable-nonfree --enable-openssl --enable-libv4l2 \
  --enable-opengl --enable-libass --disable-doc --enable-asm --enable-rpath \
  --disable-muxers --disable-demuxers --enable-demuxer=mov,flv,hls \
  --disable-encoders --disable-decoders --enable-decoder=flac,aac,opus,mp3,h264,hevc,libdav1d,hdr,srt,eac3 \
  --disable-protocols --enable-protocol=file,http,tcp,udp,hls,https,tls,httpproxy \
  --disable-filters --enable-filter=hflip,vflip,transpose --disable-avdevice \
  --disable-programs --disable-debug
make -j$(nproc)
make install

cd /tmp/libplacebo
meson setup build --prefix=$CMAKE_PREFIX_PATH --libdir=lib --buildtype=release --default-library=shared \
  -Ddebug=false  -Ddemos=false -Dtests=false -Dlcms=disabled -Dvulkan=disabled
meson compile -C build
meson install -C build

cd /tmp/mpv-0.39.0
meson setup build --prefix=$CMAKE_PREFIX_PATH --libdir=lib --buildtype=release --default-library=shared \
  -Dlibmpv=true -Dcplayer=false -Dtests=false -Ddebug=false -Dlibarchive=disabled -Dlua=enabled
meson compile -C build
meson install -C build

cd /opt
mkdir -p /tmp/deb/DEBIAN /tmp/deb/usr /tmp/deb/opt/wiliwili/lib
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$CMAKE_PREFIX_PATH -DPLATFORM_DESKTOP=ON \
  -DUSE_SYSTEM_CURL=ON -DUSE_SYSTEM_GLFW=ON -DHOMEBREW_MPV=$CMAKE_PREFIX_PATH -DVERSION_BUILD=$VERSION_BUILD \
  -DINSTALL=ON -DCUSTOM_RESOURCES_DIR=$CMAKE_PREFIX_PATH -DCMAKE_INSTALL_RPATH=$CMAKE_PREFIX_PATH/lib
cmake --build build
DESTDIR="/tmp/deb" cmake --install build

cp -d /opt/wiliwili/lib/*.so.* /tmp/deb/opt/wiliwili/lib
mv /tmp/deb/opt/wiliwili/share /tmp/deb/usr
sed -i 's|Exec=wiliwili|Exec=/opt/wiliwili/bin/wiliwili|' /tmp/deb/usr/share/applications/cn.xfangfang.wiliwili.desktop
cp scripts/deb/debian-bookworm/control /tmp/deb/DEBIAN
dpkg --build /tmp/deb wiliwili-Linux-debian-bookworm-$(uname -m).deb
