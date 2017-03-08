#!/bin/sh
set -e
doxygen
pip3 install doctr
doctr deploy

