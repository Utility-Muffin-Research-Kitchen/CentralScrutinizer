#!/bin/sh
set -eu

APP_BIN="central-scrutinizer"
PAK_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MLP1_DEFAULT_SDCARD_PATH=/mnt/sdcard

cd "$PAK_DIR"

find_sdcard_root() {
    probe=$PAK_DIR
    while [ "$probe" != "/" ] && [ -n "$probe" ]; do
        if [ -d "$probe/.system/leaf/platforms" ] ||
           [ -f "$probe/.system/leaf/launcher/env.sh" ] ||
           { [ -d "$probe/Apps" ] && [ -d "$probe/.system" ]; }; then
            printf '%s\n' "$probe"
            return 0
        fi
        probe=$(dirname "$probe")
    done

    case "$PAK_DIR" in
        */Apps/*/*.pak) (CDPATH= cd -- "$PAK_DIR/../../.." && pwd) ;;
        *) printf '%s\n' "$MLP1_DEFAULT_SDCARD_PATH" ;;
    esac
}

infer_platform() {
    case "$PAK_DIR" in
        */Apps/*/*.pak)
            platform_dir=$(basename "$(dirname "$PAK_DIR")")
            if [ "$platform_dir" != "shared" ]; then
                printf '%s\n' "$platform_dir"
                return 0
            fi
            ;;
    esac

    case "${SDCARD_PATH:-}" in
        "$MLP1_DEFAULT_SDCARD_PATH") printf '%s\n' mlp1; return 0 ;;
    esac

    printf '%s\n' "${PLATFORM:-mlp1}"
}

ARG_SDCARD_ROOT=""
PREV_ARG=""
for ARG in "$@"; do
    if [ "$PREV_ARG" = "--sdcard" ]; then
        ARG_SDCARD_ROOT="$ARG"
        break
    fi
    PREV_ARG="$ARG"
done

SDCARD_ROOT=${SDCARD_PATH:-${ARG_SDCARD_ROOT:-$(find_sdcard_root)}}
SDCARD_ROOT=${SDCARD_ROOT:-$MLP1_DEFAULT_SDCARD_PATH}
export SDCARD_PATH="$SDCARD_ROOT"

if [ -z "${PLATFORM:-}" ]; then
    PLATFORM=$(infer_platform)
fi
export PLATFORM

PLATFORM_ENV_FILE="$SDCARD_PATH/.system/leaf/platforms/$PLATFORM/launcher/env.sh"
if [ -n "${UMRK_ENV_FILE:-}" ] && [ -f "$UMRK_ENV_FILE" ]; then
    . "$UMRK_ENV_FILE"
elif [ -f "$PLATFORM_ENV_FILE" ]; then
    . "$PLATFORM_ENV_FILE"
elif [ -f "$SDCARD_PATH/.system/leaf/launcher/env.sh" ]; then
    . "$SDCARD_PATH/.system/leaf/launcher/env.sh"
fi

export SDCARD_PATH="${SDCARD_PATH:-$SDCARD_ROOT}"
export PLATFORM="${PLATFORM:-$(infer_platform)}"

if [ -z "${UMRK_PLATFORM_PATH:-}" ] && [ -n "${SYSTEM_PATH:-}" ]; then
    export UMRK_PLATFORM_PATH="$SYSTEM_PATH"
fi

if [ -z "${UMRK_PLATFORM_PATH:-}" ] && [ "$PLATFORM" != "unknown" ]; then
    export UMRK_PLATFORM_PATH="$SDCARD_PATH/.system/leaf/platforms/$PLATFORM"
fi

if [ -z "${SYSTEM_PATH:-}" ] && [ -n "${UMRK_PLATFORM_PATH:-}" ]; then
    export SYSTEM_PATH="$UMRK_PLATFORM_PATH"
fi

if [ -z "${USERDATA_PATH:-}" ] && [ -n "${UMRK_PLATFORM_PATH:-}" ]; then
    export USERDATA_PATH="$UMRK_PLATFORM_PATH/userdata"
fi

if [ -z "${SHARED_USERDATA_PATH:-}" ]; then
    export SHARED_USERDATA_PATH="$SDCARD_PATH/.system/leaf/shared/userdata"
fi

export ROMS_PATH="${ROMS_PATH:-$SDCARD_PATH/Roms}"
export IMAGES_PATH="${IMAGES_PATH:-$SDCARD_PATH/Images}"
export APPS_PATH="${APPS_PATH:-$SDCARD_PATH/Apps}"
export SAVES_PATH="${SAVES_PATH:-$SDCARD_PATH/Saves}"
export STATES_PATH="${STATES_PATH:-$SDCARD_PATH/States}"
export BIOS_PATH="${BIOS_PATH:-$SDCARD_PATH/Bios}"
export CHEATS_PATH="${CHEATS_PATH:-$SDCARD_PATH/Cheats}"

if [ -n "${UMRK_PLATFORM_PATH:-}" ] && [ -z "${CORES_PATH:-}" ]; then
    export CORES_PATH="$UMRK_PLATFORM_PATH/cores"
fi

if [ -n "${UMRK_PLATFORM_PATH:-}" ] && [ -z "${INFO_PATH:-}" ]; then
    export INFO_PATH="$UMRK_PLATFORM_PATH/info"
fi

if [ -n "${USERDATA_PATH:-}" ] && [ -z "${LOGS_PATH:-}" ]; then
    export LOGS_PATH="$USERDATA_PATH/logs"
fi

if [ -z "${CS_WEB_ROOT:-}" ] && [ -d "$PAK_DIR/resources/web" ]; then
    export CS_WEB_ROOT="$PAK_DIR/resources/web"
fi

LOG_ROOT=${LOGS_PATH:-"$USERDATA_PATH/logs"}
mkdir -p "$LOG_ROOT"
LOG_FILE="$LOG_ROOT/$APP_BIN.txt"
: >"$LOG_FILE"

exec >>"$LOG_FILE"
exec 2>&1

echo "=== Launching Central Scrutinizer at $(date) ==="
echo "platform=${PLATFORM:-unknown} device=${DEVICE:-unknown}"
echo "args: $*"

if [ -x "$PAK_DIR/bin/$APP_BIN" ]; then
    exec "$PAK_DIR/bin/$APP_BIN" "$@"
fi

exec "$PAK_DIR/$APP_BIN" "$@"
