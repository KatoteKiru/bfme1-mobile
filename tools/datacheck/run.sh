#!/bin/sh
# Run datacheck from anywhere: ./tools/datacheck/run.sh [--game DIR] [--out DIR]
cd "$(dirname "$0")" && exec python3 -m datacheck "$@"
