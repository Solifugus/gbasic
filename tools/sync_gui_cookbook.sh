#!/usr/bin/env bash
# Expand docs/gui_cookbook.md from examples/gui_cookbook/. Wraps the shared,
# parameterised harness (tools/sync_xlsx_cookbook.sh); see that file for how
# the markers work and why the page owns neither the code nor the output.
set -euo pipefail
cd "$(dirname "$0")/.."
exec tools/sync_xlsx_cookbook.sh docs/gui_cookbook.md examples/gui_cookbook
