#!/usr/bin/env bash
# The credit cookbook shares the xlsx cookbook's sync harness -- one
# implementation, so the cookbooks cannot drift apart in behaviour.
exec "$(dirname "$0")/sync_xlsx_cookbook.sh" docs/credit_cookbook.md examples/credit_cookbook
