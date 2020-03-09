#!/bin/sh

set -e

readonly version="1.10.2"

case "$( uname -s )" in
    Linux)
        shatool="sha256sum"
        sha256sum="763464859c7ef2ea3a0a10f4df40d2025d3bb9438fcb1228404640410c0ec22d"
        platform="linux"
        ;;
    *)
        echo "Unrecognized platform $( uname -s )"
        exit 1
        ;;
esac
readonly shatool
readonly sha256sum
readonly platform

readonly filename="ninja-$platform"
readonly tarball="$filename.zip"

cd .gitlab

echo "$sha256sum  $tarball" > ninja.sha256sum
curl -OL "https://github.com/ninja-build/ninja/releases/download/v$version/$tarball"
$shatool --check ninja.sha256sum
./cmake/bin/cmake -E tar xf "$tarball"
