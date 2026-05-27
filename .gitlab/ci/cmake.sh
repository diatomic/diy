#!/bin/sh

set -e

readonly version="3.19.7"

case "$( uname -s )" in
    Linux)
        shatool="sha256sum"
        platform="Linux"
        case "$( uname -m )" in
            aarch64)
                sha256sum="eb1cf718eca1d5bc212a0ef76d19a977b6b6481a795985b8741c31f866c88e09"
                arch="aarch64"
                ;;
            *)
                sha256sum="ba4a5f46aab500e0d8d952ee735dcfb0c870d326e851addc037c99eb1ea4b66c"
                arch="x86_64"
                ;;
        esac
        ;;
    *)
        echo "Unrecognized platform $( uname -s )"
        exit 1
        ;;
esac
readonly shatool
readonly sha256sum
readonly platform
readonly arch

readonly filename="cmake-$version-$platform-$arch"
readonly tarball="$filename.tar.gz"

cd .gitlab

echo "$sha256sum  $tarball" > cmake.sha256sum
curl -OL "https://github.com/Kitware/CMake/releases/download/v$version/$tarball"
$shatool --check cmake.sha256sum
tar xf "$tarball"
mv "$filename" cmake
