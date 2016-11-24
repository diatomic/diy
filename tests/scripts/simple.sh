#!/bin/bash

for np in 2 3; do
    let nb=${np}+1
    ${1} -np ${np} ./simple-test -b ${nb}
done
