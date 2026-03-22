#!/usr/bin/env bash
# Docker entry point for the Vita crash analysis tool.
# Installs Python dependencies (cached in /tools volume) then runs the tool.
set -e

PYLIB=/tools/pylib

# Install pyelftools if not already cached
if ! PYTHONPATH="$PYLIB" python3 -c 'import elftools' 2>/dev/null; then
    echo "Installing pyelftools..." >&2
    pip3 install --quiet --target="$PYLIB" -r /src/tools/vita_crash/requirements.txt
fi

export PYTHONPATH="$PYLIB"
exec python3 -m vita_crash "$@"
