#!/bin/bash
set -e
pushd "$(dirname $(dirname $(realpath -s $0)))" > /dev/null
ninja coverage -C build/
python3 scripts/clean_coverage_report.py build/meson-logs/coveragereport/index.html
popd >/dev/null
