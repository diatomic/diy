#!/bin/bash

MAXP=3

if [[ (`uname` == 'Darwin') && ($MPI == 'openmpi') ]]; then
    MAXP=2
fi

for p in $(seq 1 $MAXP); do
  for b in 2 4 8 9 12 24 36 44 48 56 64; do
      $1 -np $p ./merge-swap-reduce-test -b $b
  done
done
