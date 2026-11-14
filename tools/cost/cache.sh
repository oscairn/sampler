#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Data-cache misses of the DSP core with valgrind's cache simulator (cachegrind's, run through callgrind so
# that each measured 128-frame block is its own dump), configured to the target device's caches: 32 KB
# 4-way first-level data cache, one 512 KB 16-way last-level cache, 64-byte lines (docs/cost.md).
# Runs on aarch64 Linux with g++ and valgrind, as cg.sh:
#   docker run --rm -v "$PWD":/src:ro --tmpfs /out:exec IMAGE sh /src/tools/cost/cache.sh /src /out [check]
# Prints, for 1, 8, 32 and 64 held voices at mixed pitches (keys 48-72), the first-level and last-level data
# read misses per block and per voice-block (mean over the measured blocks, the first dropped). With "check"
# it fails (exit 1) when a case's misses per voice-block are more than 25 % over tools/cost/cache-baseline.txt
# plus 2 (the simulator is deterministic; the slack is for compiler changes).
set -e
SRC=$1; OUT=$2; MODE=$3
F="-std=c++17 -O3 -DNDEBUG -mcpu=cortex-a53"
CACHES="--D1=32768,4,64 --LL=524288,16,64 --I1=32768,2,64"
mkdir -p "$OUT/cache"
[ -x "$OUT/pcbench" ] || g++ $F "$SRC"/core/*.cpp "$SRC/tools/pcbench.cpp" -o "$OUT/pcbench"
run() {  # name voices plan interp
  n=$1; d="$OUT/cache/$n"; mkdir -p "$d"
  valgrind --tool=callgrind --cache-sim=yes $CACHES --dump-line=no --callgrind-out-file="$d/cg.out" "$OUT/pcbench" cg held "$2" "$3" "$4" 0.3 0.3 "$d/v.tsv" > "$d/out.txt" 2> "$d/err.txt"
  # the events line names the columns of each dump's totals line
  ev=$(sed -n 's/^events: //p' "$d/cg.out.1" | head -1)
  ls "$d"/cg.out.* | sort -t. -k3 -n | while read -r f; do sed -n "s/^totals: //p" "$f"; done > "$d/blocks.txt"
  echo "$ev" | awk -v n="$n" -v v="$2" -v f="$d/blocks.txt" '
    { for (i = 1; i <= NF; ++i) col[$i] = i }
    END {
      while ((getline line < f) > 0) { if (++b == 1) continue; split(line, x, " "); ir += x[col["Ir"]]; dr += x[col["Dr"]]; d1 += x[col["D1mr"]]; ll += x[col["DLmr"]]; c++ }
      printf "%-14s voices %2d  Ir %9.0f  Dr %8.0f  D1mr %7.0f  DLmr %6.0f  per voice-block: D1mr %7.1f  DLmr %6.1f\n", n, v, ir / c, dr / c, d1 / c, ll / c, d1 / c / v, ll / c / v }'
  find "$d" -name 'cg.out*' -delete
}
{
for v in 1 8 32 64; do run "mono-sinc-$v" $v mono sinc; done
for v in 1 8 32 64; do run "stereo-sinc-$v" $v stereo sinc; done
if [ "$MODE" != check ]; then
  for v in 1 8 32 64; do run "mono-herm-$v" $v mono herm; done
  for v in 16; do run "mono-sinc-$v" $v mono sinc; run "stereo-sinc-$v" $v stereo sinc; done
fi
} | tee "$OUT/cache-summary.txt"
if [ "$MODE" = check ]; then
  fail=0
  while read -r name d1 ll; do
    case "$name" in ''|'#'*) continue;; esac
    set -- $(awk -v n="$name" '$1==n {print $15, $17}' "$OUT/cache-summary.txt")
    if [ -z "$1" ]; then echo "MISSING $name"; fail=1; continue; fi
    for k in "D1mr $1 $d1" "DLmr $2 $ll"; do
      set -- $k
      if [ "$(awk -v g="$2" -v b="$3" 'BEGIN {print (g > b * 1.25 + 2)}')" = 1 ]; then echo "REGRESSION $name $1: $2 a voice-block, baseline $3"; fail=1
      else echo "ok $name $1: $2 (baseline $3)"; fi
    done
  done < "$SRC/tools/cost/cache-baseline.txt"
  exit $fail
fi
