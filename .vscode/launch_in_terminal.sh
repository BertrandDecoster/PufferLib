#!/bin/bash
# Launch script for running demos in external Terminal.app
DIR="$1"
shift
CMD="$@"

osascript -e "tell application \"Terminal\"
    activate
    do script \"cd '$DIR' && $CMD\"
end tell"
