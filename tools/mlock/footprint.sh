#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# The locked footprint of a large pack (Linux; the acceptance check for reading packs without mapping them).
#   sh tools/mlock/footprint.sh SRC BUILD WORK [N]
# SRC the source tree, BUILD a configured build of it (for libsamplepack_capi.a and the samplepack tool), WORK a
# scratch folder with room for about 2 x N x 11 MB; N noise files of 60 s (default 40: a pack of about 450 MB).
# Run where mlockall may lock that much, for example `docker run --ulimit memlock=-1 ...`.
set -eu
SRC=$1; BUILD=$2; WORK=$3; N=${4:-40}
L=$(find "$BUILD" -name libsamplepack_capi.a | head -1); CLI=$(find "$BUILD" -path '*release/samplepack' -type f | head -1)
mkdir -p "$WORK/wav"
g++ -std=c++17 -O2 -I"$SRC/src" -I"$SRC/third_party/samplepack/include" "$SRC/tools/mlock/footprint.cpp" "$SRC/src/packs.cpp" "$L" -lpthread -ldl -lm -o "$WORK/footprint"
[ -f "$WORK/big.osp" ] || { "$WORK/footprint" gen "$WORK/wav" "$N"; "$CLI" pack -o "$WORK/big.osp" "$WORK"/wav/*.wav > /dev/null; rm -rf "$WORK/wav"; }
ls -l "$WORK/big.osp"
"$WORK/footprint" measure "$WORK/big.osp"
