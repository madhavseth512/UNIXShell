#!/bin/bash
# Run every suite. Requires a build first (make).
cd "$(dirname "$0")/.." || exit 1
rc=0

echo "########## behaviour ##########"
bash tests/run.sh    || rc=1
echo
echo "########## signals (pty) ##########"
python3 tests/signals.py  || rc=1
echo
echo "########## process groups ##########"
python3 tests/pgroups.py  || rc=1
echo
echo "########## memory (valgrind) ##########"
bash tests/memory.sh || rc=1

echo
[ "$rc" -eq 0 ] && echo "ALL SUITES PASSED" || echo "SOME SUITES FAILED"
exit $rc
