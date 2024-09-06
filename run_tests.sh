#!/bin/bash

make --silent

[ -x ./mdlinks ] || { echo "Error: ./mdlinks executable not found or not executable." >&2; exit 1; }
[ -d ../learnxinyminutes/source/docs/ ] || { echo "Error: ../learnxinyminutes/source/docs/ directory not found." >&2; exit 1; }

./mdlinks ../learnxinyminutes/source/docs/ | wc -l
{ time ./mdlinks ../learnxinyminutes/source/docs/ > /dev/null; } 2>&1 | grep '^real' | sed 's/^real[[:space:]]*//'
