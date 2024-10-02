#!/bin/bash
set -e

BUILD_DIR=cmake-build-switch

# cd to wiliwili
cd "$(dirname $0)/.."
git config --global --add safe.directory `pwd`

BASE_URL="https://github.com/dragonflylee/mpv-portlibs/releases/download/switch-portlibs/"

PKGS=(
    "libuam-master-1-any.pkg.tar.zst"
    "switch-harfbuzz-10.0.1-1-any.pkg.tar.zst"
    "switch-ffmpeg-7.1-1-any.pkg.tar.zst"
    "switch-libmpv-deko3d-0.36.0-6-any.pkg.tar.zst"
    "switch-nspmini-main-1-any.pkg.tar.zst"
    "hacBrewPack-3.05-1-any.pkg.tar.zst"
)

dkp-pacman -R --noconfirm switch-libmpv
for PKG in "${PKGS[@]}"; do
    [ -f "${PKG}" ] || curl -LO ${BASE_URL}${PKG}
    dkp-pacman -U --noconfirm ${PKG}
done

cmake -B ${BUILD_DIR} -DCMAKE_BUILD_TYPE=Release -DBUILTIN_NSP=ON -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON -DBRLS_UNITY_BUILD=OFF
make -C ${BUILD_DIR} wiliwili.nro -j$(nproc)