#!/bin/sh
set -e
doxygen
pip3 install doctr
doctr deploy --built-docs docs/html --gh-pages-docs .

