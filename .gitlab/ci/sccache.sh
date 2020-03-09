#!/bin/sh

set -e

case "$( uname -s )" in
    Linux)
        version="0.2.13"
        shatool="sha256sum"
        sha256sum="28a5499e340865b08b632306b435913beb590fbd7b49a3f887a623b459fabdeb"
        platform="x86_64-unknown-linux-musl"
        ;;
    *)
        echo "Unrecognized platform $( uname -s )"
        exit 1
        ;;
esac
readonly version
readonly shatool
readonly sha256sum
readonly platform

readonly filename="sccache-$version-$platform"
readonly tarball="$filename.tar.gz"

if [ "$( uname -s )" = "Darwin" ]; then
    url="https://paraview.org/files/dependencies"
else
    url="https://github.com/mozilla/sccache/releases/download/$version"
fi
readonly url

cd .gitlab

echo "$sha256sum  $tarball" > sccache.sha256sum
curl -OL "$url/$tarball"
$shatool --check sccache.sha256sum
tar xf "$tarball"
mv "$filename/sccache" .
