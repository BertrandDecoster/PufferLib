#!/bin/bash
# Launch a demo in an external terminal window.
# Usage: launch_in_terminal.sh <DIR> <CMD...>
# Supports: macOS (Terminal.app), Windows (cmd via git bash), Linux (gnome-terminal/xterm).

DIR="$1"
shift
CMD="$*"

case "$(uname -s)" in
    Darwin*)
        osascript -e "tell application \"Terminal\"
            activate
            do script \"cd '$DIR' && $CMD\"
        end tell"
        ;;
    MINGW*|MSYS*|CYGWIN*)
        WIN_DIR=$(cygpath -w "$DIR")
        # cmd.exe wants backslashes in local-exec paths (.\foo.exe, not ./foo.exe).
        CMD_WIN="${CMD//.\//.\\}"
        # Write a temp .bat and hand cmd the file path. This avoids the
        # bash->cmd quoting mess: MSYS escapes embedded quotes as \", but
        # cmd expects "" — so any inline `cmd /k "cd /d \"...\" && ..."`
        # gets mangled and errors with "filename syntax is incorrect".
        BAT_FILE="/tmp/launch_companions_$$.bat"
        {
            printf '@echo off\r\n'
            printf 'cd /d "%s"\r\n' "$WIN_DIR"
            printf '%s\r\n' "$CMD_WIN"
            printf 'echo.\r\n'
            printf 'pause\r\n'
        } > "$BAT_FILE"
        BAT_WIN=$(cygpath -w "$BAT_FILE")
        cmd //c start "" "$BAT_WIN"
        ;;
    Linux*)
        if command -v gnome-terminal >/dev/null 2>&1; then
            gnome-terminal -- bash -c "cd '$DIR' && $CMD; exec bash"
        elif command -v konsole >/dev/null 2>&1; then
            konsole -e bash -c "cd '$DIR' && $CMD; exec bash" &
        elif command -v xterm >/dev/null 2>&1; then
            xterm -e "cd '$DIR' && $CMD; bash" &
        else
            echo "No supported terminal emulator found (gnome-terminal, konsole, xterm)" >&2
            exit 1
        fi
        ;;
    *)
        echo "Unsupported platform: $(uname -s)" >&2
        exit 1
        ;;
esac
