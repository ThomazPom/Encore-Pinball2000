#!/usr/bin/env bash
# Extract last-snap delivery / drift / iret_raise / pdb05_wall metrics
# from a partc log. Usage: scripts/extract-metrics.sh <log...>
set -u
for f in "$@"; do
  base=$(basename "$f" .log)
  delivery=$(grep -aoE 'delivery=[0-9.]+%' "$f" | tail -1 | sed 's/delivery=//')
  drift=$(grep -aoE 'drift=[0-9.]+ \| delta' "$f" | tail -1 | awk '{print $1}' | sed 's/drift=//')
  iret=$(grep -a 'iret_raise' "$f" | tail -1 | grep -oE 'p50=[0-9]+us p95=[0-9]+us p99=[0-9]+us max=[0-9]+us')
  pdb05=$(grep -a 'pdb05_wall_delta' "$f" | tail -1 | grep -oE 'p50=[0-9]+us p95=[0-9]+us p99=[0-9]+us max=[0-9]+us')
  win=$(grep -a 'p2k-iret-window' "$f" | tail -1 | grep -oE 'arms=[0-9]+ tbs_in_window=[0-9]+')
  printf "%-32s delivery=%-7s drift=%-6s\n  iret %s\n  pdb05 %s\n  %s\n\n" \
      "$base" "${delivery:-?}" "${drift:-?}" "${iret:-?}" "${pdb05:-?}" "${win:-no-window}"
done
