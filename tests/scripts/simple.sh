#!/bin/bash
for np in 2 3; do
    let nb=${np}+1
    if [ $# -eq 0 ]
    then
      ./simple-test -b ${nb}
    else
      ${1} -np ${np} ./simple-test -b ${nb}
    fi
done
