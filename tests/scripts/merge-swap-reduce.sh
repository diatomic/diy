#!/bin/bash

for p in 1 2 3; do
  for b in 2 4 8 9 12 24 36 44 48 56 64; do
      $1 -np $p ./merge-swap-reduce-test -b $b
  done
done
