#!/usr/bin/env bash
# Verify or run the native-tft build using the sibling meshtastic-firmware repo.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  build-verify/native-tft-docker.sh build
  build-verify/native-tft-docker.sh run
  build-verify/native-tft-docker.sh shell

Modes:
  build  Build PlatformIO env native-tft inside Docker.
  run    Build if needed, then launch meshtasticd with XQuartz/X11 forwarding.
  shell  Open an interactive shell in the prepared Docker image.

Environment overrides:
  MESHTASTIC_FIRMWARE_DIR  Path to meshtastic-firmware checkout.
  MESHTASTIC_NATIVE_IMAGE  Docker image name to use/build. Default: meshtastic-native
  PLATFORMIO_ENV           PlatformIO environment. Default: native-tft
EOF
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEVICE_UI_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FIRMWARE_DIR_INPUT="${MESHTASTIC_FIRMWARE_DIR:-$DEVICE_UI_DIR/../meshtastic-firmware}"
IMAGE_NAME="${MESHTASTIC_NATIVE_IMAGE:-meshtastic-native}"
PLATFORMIO_ENV="${PLATFORMIO_ENV:-native-tft}"
MODE="${1:-build}"

if [ "$MODE" = "--help" ] || [ "$MODE" = "-h" ]; then
    usage
    exit 0
fi

case "$MODE" in
    build|run|shell)
        ;;
    *)
        usage
        die "Unknown mode: $MODE"
        ;;
esac

need_cmd docker

[ -d "$FIRMWARE_DIR_INPUT" ] || die "meshtastic-firmware checkout not found at $FIRMWARE_DIR_INPUT"
FIRMWARE_DIR="$(cd "$FIRMWARE_DIR_INPUT" && pwd)"
[ -f "$FIRMWARE_DIR/Dockerfile.native" ] || die "Dockerfile.native not found in $FIRMWARE_DIR"
[ -f "$FIRMWARE_DIR/platformio.ini" ] || die "platformio.ini not found in $FIRMWARE_DIR"

ensure_image() {
    if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
        echo "Building Docker image $IMAGE_NAME from $FIRMWARE_DIR/Dockerfile.native"
        docker build -t "$IMAGE_NAME" -f "$FIRMWARE_DIR/Dockerfile.native" "$FIRMWARE_DIR"
    fi
}

docker_pio() {
    local command="$1"
    docker run --rm \
        -v "$FIRMWARE_DIR:/firmware" \
        -v "$DEVICE_UI_DIR:/device-ui" \
        "$IMAGE_NAME" \
        bash -lc "cd /firmware && $command"
}

prepare_portduino_fs() {
    PORTDUINO_FS_DIR="/tmp/meshtastic-portduino-fs"
    rm -rf "$PORTDUINO_FS_DIR"
    mkdir -p "$PORTDUINO_FS_DIR/apps"
    cp -R "$DEVICE_UI_DIR/source/apps/scripts" "$PORTDUINO_FS_DIR/apps/scripts"
}

ensure_xquartz() {
    need_cmd pgrep
    need_cmd ls
    need_cmd awk
    need_cmd sed

    if ! pgrep -q Xquartz; then
        if command -v open >/dev/null 2>&1; then
            echo "Starting XQuartz..."
            open -a XQuartz
            sleep 4
        fi
    fi

    X_DISPLAY="$(ls /tmp/.X11-unix/ 2>/dev/null | tail -1 | sed 's/^X//')"
    SERVERAUTH="$(ls -t "$HOME"/.serverauth.* 2>/dev/null | head -1)"

    [ -n "${X_DISPLAY:-}" ] || die "XQuartz display not found under /tmp/.X11-unix"
    [ -n "${SERVERAUTH:-}" ] || die "XQuartz auth file not found under \$HOME/.serverauth.*"

    echo "Using XQuartz display :$X_DISPLAY"

    DISPLAY="/tmp/.X11-unix/X$X_DISPLAY" XAUTHORITY="$SERVERAUTH" /opt/X11/bin/xhost + 2>/dev/null || true

    COOKIE="$(DISPLAY="/tmp/.X11-unix/X$X_DISPLAY" XAUTHORITY="$SERVERAUTH" /opt/X11/bin/xauth list 2>/dev/null | head -1 | awk '{print $3}')"
    [ -n "${COOKIE:-}" ] || die "Failed to extract X11 cookie from XQuartz"

    XAUTH_DIR="/tmp/docker_xauth_out"
    mkdir -p "$XAUTH_DIR"
    docker run --rm -v "$XAUTH_DIR:/out" "$IMAGE_NAME" bash -lc "
        xauth add host.docker.internal:$X_DISPLAY MIT-MAGIC-COOKIE-1 $COOKIE
        xauth add host.docker.internal/unix:$X_DISPLAY MIT-MAGIC-COOKIE-1 $COOKIE
        cp ~/.Xauthority /out/xauth
    " >/dev/null 2>&1
}

run_build() {
    echo "Building PlatformIO env $PLATFORMIO_ENV"
    docker_pio "pio run -e $PLATFORMIO_ENV"
}

run_shell() {
    docker run --rm -it \
        -v "$FIRMWARE_DIR:/firmware" \
        -v "$DEVICE_UI_DIR:/device-ui" \
        "$IMAGE_NAME" \
        bash
}

run_native() {
    ensure_xquartz
    prepare_portduino_fs

    docker run --rm -it \
        --name meshtastic-tft \
        -e DISPLAY="host.docker.internal:$X_DISPLAY" \
        -e XAUTHORITY=/root/.Xauthority \
        -v "/tmp/docker_xauth_out/xauth:/root/.Xauthority:ro" \
        -v "$PORTDUINO_FS_DIR:/portduino-fs" \
        -v "$FIRMWARE_DIR:/firmware" \
        -v "$DEVICE_UI_DIR:/device-ui" \
        "$IMAGE_NAME" \
        bash -lc '
            cd /firmware
            if [ ! -f .pio/build/'"$PLATFORMIO_ENV"'/meshtasticd ]; then
                echo "=== Building '"$PLATFORMIO_ENV"' ==="
                pio run -e '"$PLATFORMIO_ENV"'
            fi
            echo "=== Running meshtasticd ==="
            .pio/build/'"$PLATFORMIO_ENV"'/meshtasticd -d /portduino-fs -c /firmware/config.yaml
        '
}

ensure_image

case "$MODE" in
    build)
        run_build
        ;;
    run)
        run_build
        run_native
        ;;
    shell)
        run_shell
        ;;
esac
