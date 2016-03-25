#!/bin/bash

for b in 2 4 8 64; do
    ./kd-tree-test $@ -b $b
done
