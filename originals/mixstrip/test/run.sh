#!/bin/sh
# Mix Strip, Mix Bus and Mix Desk together (test/mix_test.c), under ASan and UBSan in Docker gcc:12 (Apple clang's ASan hangs
# on this Mac). From anywhere: originals/mixstrip/test/run.sh. Prints PASSED or the failures.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
for h in mixshm.h mixin.h; do
  for d in mixbus mixdesk; do
    cmp -s "$HERE/src/$h" "$HERE/$d/src/$h" || { echo "FAILED: src/$h and $d/src/$h differ (keep them the same)"; exit 1; }
  done
done
docker run --rm -v "$HERE:/p" -w /p gcc:12 sh -ec '
  F="-std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all"
  gcc $F -Dmpc_engine=strip_engine -Dmpc_engine_input=strip_input -Dmpc_engine_changed=strip_changed -Isrc \
      -c src/mixstrip.c -o /tmp/strip.o
  gcc $F -Dmpc_engine=bus_engine -Dmpc_engine_input=bus_input -Imixbus/src -c mixbus/src/mixbus.c -o /tmp/bus.o
  gcc $F -Dmpc_engine=desk_engine -Dmpc_engine_input=desk_input -Imixdesk/src -c mixdesk/src/mixdesk.c -o /tmp/desk.o
  gcc $F test/mix_test.c /tmp/strip.o /tmp/bus.o /tmp/desk.o -o /tmp/mix_test -lm -ldl
  ASAN_OPTIONS=detect_leaks=1 /tmp/mix_test'
