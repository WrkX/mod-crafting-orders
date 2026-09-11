#!/bin/bash
set -e
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
g++ -std=c++17 -O0 -g0 -Wall -Wextra -Werror \
  -DPROTOCOL_VERSION=10 \
  -I "${SCRIPT_DIR}/../src" \
  -o /tmp/test_crafting_orders \
  "${SCRIPT_DIR}/test_crafting_orders.cpp"
/tmp/test_crafting_orders
