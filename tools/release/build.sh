#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Build one platform's single-file .clap from a clean Release configure.
#
#   build.sh <platform> <outdir>      e.g. build.sh macos-arm64 dist
#
# Writes <outdir>/<NAME>-<platform>.clap; the CMake tree goes under <outdir>/build-<platform>.
# Must run on a machine of the platform's OS (each release job runs on its own runner).
set -eu
cd "$(dirname "$0")/../.."
. tools/release/release.conf

platform=${1:?usage: build.sh <platform> <outdir>}
out=${2:?usage: build.sh <platform> <outdir>}

case $platform in
  macos-*) want=Darwin ;;
  linux-*) want=Linux ;;
  windows-*) want=Windows ;;
  *) echo "build.sh: unknown platform '$platform' (macos-*, linux-* or windows-*)" >&2; exit 2 ;;
esac
os=$(uname -s)
case $os in MINGW*|MSYS*|CYGWIN*) os=Windows ;; esac
[ "$os" = "$want" ] || {
    echo "build.sh: platform '$platform' needs $want, this is $os; build each platform on its own runner" >&2
    exit 1; }

mkdir -p "$out"
build=$out/build-$platform
rm -rf "$build"
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" --parallel --config Release
clap=$build/$CLAP_TARGET.clap
[ -f "$clap" ] || clap=$build/Release/$CLAP_TARGET.clap  # multi-config generators (Visual Studio)
cp "$clap" "$out/$NAME-$platform.clap"
echo "build.sh: built $out/$NAME-$platform.clap"
