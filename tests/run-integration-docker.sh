#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Run the integration suite in an isolated Docker workspace. The argument
# names the ZMK variant: upstream (the default) or dya.
# ZMK_TEST_WORKSPACE_VOLUME names a Docker volume that keeps the west
# workspace between runs, so that a rerun updates it rather than fetching it.

set -euo pipefail

# Git Bash rewrites arguments that look like POSIX paths before Docker sees
# them, which turns the container's /src into a path under the Git install.
export MSYS_NO_PATHCONV=1

repo_root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
image="${ZMK_TEST_IMAGE:-zmkfirmware/zmk-build-arm:stable}"
workspace=()

if [ -n "${ZMK_TEST_WORKSPACE_VOLUME:-}" ]; then
    workspace=(--volume "$ZMK_TEST_WORKSPACE_VOLUME:/workspace" --env ZMK_TEST_WORKSPACE=/workspace)
fi

docker run --rm \
    --volume "$repo_root:/src:ro" \
    ${workspace[@]+"${workspace[@]}"} \
    "$image" \
    /bin/bash /src/tests/integration/run.sh "$@"
