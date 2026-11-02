#!/bin/sh
set -eu

SKETCH_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -z "${ARDUINO_CLI:-}" ]; then
    if command -v arduino-cli >/dev/null 2>&1; then
        ARDUINO_CLI=$(command -v arduino-cli)
    else
        ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
    fi
fi
if [ ! -x "$ARDUINO_CLI" ]; then
    echo 'Set ARDUINO_CLI to the Arduino CLI executable.' >&2
    exit 1
fi

# megaTinyCore 2.6.9 fixes PA0 as UPDI by default; its resetpin menu is hidden.
# Compilation does not program the chip or apply fuses.
FQBN='megaTinyCore:megaavr:atxy2:chip=412,clock=10internal,millis=enabled,startuptime=8,bodvoltage=4v2,bodmode=enabled,eesave=enable,printf=default,wiremode=mors,WDTtimeout=disabled,WDTwindow=disabled,PWMmux=default,attach=allenabled'
exec "$ARDUINO_CLI" compile --fqbn "$FQBN" --warnings all \
    --build-path "${BUILD_DIR:-$SKETCH_DIR/build}" "$SKETCH_DIR"
