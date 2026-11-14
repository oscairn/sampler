#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Instruction cost of the DSP core with callgrind (the regression gate; docs/cost.md).
# Runs on aarch64 Linux with g++ and valgrind, e.g. in the device-like container:
#   docker run --rm -v "$PWD":/src:ro --tmpfs /out:exec IMAGE sh /src/tools/cost/cg.sh /src /out [check]
# Builds tools/pcbench.cpp at -O3 -mcpu=cortex-a53, runs every case with one callgrind dump per
# measured 128-frame block, and prints instructions per block (mean, max). With "check" it runs the
# gated cases and fails (exit 1) when a case's mean is more than 10 % over tools/cost/baseline.txt or
# a per-voice gate (docs/cost.md) is broken.
set -e
SRC=$1; OUT=$2; MODE=$3
F="-std=c++17 -O3 -DNDEBUG -mcpu=cortex-a53"
mkdir -p "$OUT/runs"
g++ $F "$SRC"/core/*.cpp "$SRC/tools/pcbench.cpp" -o "$OUT/pcbench"
run() {  # name args...
  n=$1; shift; d="$OUT/runs/$n"; mkdir -p "$d"
  valgrind --tool=callgrind --dump-line=no --callgrind-out-file="$d/cg.out" "$OUT/pcbench" cg "$@" "$d/v.tsv" > "$d/out.txt" 2> "$d/err.txt"
  # per-block totals (dumps 1..N); the first measured block is dropped (warm caches of the tool itself)
  ls "$d"/cg.out.* | sort -t. -k3 -n | while read -r f; do sed -n "s/^totals: \([0-9]*\).*/\1/p" "$f"; done > "$d/blocks.txt"
  awk -v n="$n" 'NR>1 {s+=$1; c++; if ($1>m) m=$1} END {printf "%-18s blocks %5d  mean %9.0f  max %9.0f\n", n, c, s/c, m}' "$d/blocks.txt"
  case "$n" in shed*) echo "  $n blocks from the shed block on: $(head -4 "$d/blocks.txt" | tr "\n" " ")";; esac
  find "$d" -name 'cg.out.*' -delete
}
{
run rest rest mono sinc 0.3 0.3
if [ "$MODE" = check ]; then
  for p in mono stereo; do for v in 8 32; do run "$p-sinc-$v" held $v $p sinc 0.3 0.3; done; done
else
  for p in mono stereo; do for i in sinc herm; do for v in 1 4 8 16 32 64; do run "$p-$i-$v" held $v $p $i 0.3 0.3; done; done; done
  for p in mono-thru stereo-thru; do for v in 8 32; do run "$p-sinc-$v" held $v $p sinc 0.3 0.3; done; done
  for p in mono stereo; do for v in 8 32; do run "$p-release-$v" release $v $p sinc 0.3 0.3; done; done
  for p in mono-t4 stereo-t4 mono-t5 stereo-t5; do for v in 8 32; do run "$p-release-$v" release $v $p sinc 0.3 0.3; done; done
  for p in mono-eq stereo-eq; do for v in 8 32; do run "$p-sinc-$v" held $v $p sinc 0.3 0.3; done; done
fi
for v in 16 32; do run "shed$v" shed $v mono sinc 0.3 0.05; done
} | tee "$OUT/summary.txt"
if [ "$MODE" = check ]; then
  fail=0
  while read -r name base; do
    case "$name" in ''|'#'*) continue;; esac
    got=$(awk -v n="$name" '$1==n {print $5}' "$OUT/summary.txt")
    if [ -z "$got" ] || [ "$(awk -v g="$got" -v b="$base" 'BEGIN {print (g > b * 1.10)}')" = 1 ]; then
      echo "REGRESSION $name: $got instructions a block, baseline $base (+10 % allowed)"; fail=1
    else echo "ok $name: $got (baseline $base)"; fi
  done < "$SRC/tools/cost/baseline.txt"
  # the per-voice gates, from 8 to 32 held voices
  for p in mono stereo; do
    a=$(awk -v n="$p-sinc-8" '$1==n {print $5}' "$OUT/summary.txt"); b=$(awk -v n="$p-sinc-32" '$1==n {print $5}' "$OUT/summary.txt")
    gate=11000; [ $p = stereo ] && gate=19500
    per=$(awk -v a="$a" -v b="$b" 'BEGIN {printf "%.0f", (b - a) / 24}')
    if [ "$per" -gt "$gate" ]; then echo "GATE $p: $per instructions a voice, gate $gate"; fail=1; else echo "ok gate $p: $per a voice (gate $gate)"; fi
  done
  rest=$(awk '$1=="rest" {print $5}' "$OUT/summary.txt")
  if [ "$(awk -v r="$rest" 'BEGIN {print (r > 200)}')" = 1 ]; then echo "GATE rest: $rest (gate 200)"; fail=1; else echo "ok gate rest: $rest (gate 200)"; fi
  # shedding (every held voice shed in one block): the shed block and the next over two blocks at rest, a voice
  for v in 16 32; do
    set -- $(head -2 "$OUT/runs/shed$v/blocks.txt" | tr "\n" " ")
    per=$(awk -v a="$1" -v b="$2" -v r="$rest" -v v="$v" 'BEGIN {printf "%.0f", (a + b - 2 * r) / v}')
    if [ "$per" -gt 2000 ]; then echo "GATE shed$v: $per instructions a shed voice, gate 2000"; fail=1; else echo "ok gate shed$v: $per a shed voice (gate 2000)"; fi
  done
  exit $fail
fi
