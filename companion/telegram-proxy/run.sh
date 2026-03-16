#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_PYTHON="$SCRIPT_DIR/.venv/bin/python"
SERVER_SCRIPT="$SCRIPT_DIR/server.py"

if [[ ! -x "$VENV_PYTHON" ]]; then
    echo "Local virtualenv is missing. Expected: $VENV_PYTHON" >&2
    echo "Create it first with:" >&2
    echo "  python3 -m venv \"$SCRIPT_DIR/.venv\"" >&2
    echo "  \"$SCRIPT_DIR/.venv/bin/pip\" install -r \"$SCRIPT_DIR/requirements.txt\"" >&2
    exit 1
fi

cd "$SCRIPT_DIR"
exec "$VENV_PYTHON" "$SERVER_SCRIPT"
