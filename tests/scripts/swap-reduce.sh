#!/bin/bash

for b in 2 4 8 9 12 24 36 44 48 56 64; do
    ./swap-reduce-test $@ -b $b
done
