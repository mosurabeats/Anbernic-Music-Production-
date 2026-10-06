#!/bin/bash
# PortMaster launcher for ARDKORE. Copy this script to your ports folder and
# the ardkore/ folder (binary + samples/) next to it.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/ardkore"
cd "$GAMEDIR" || exit 1
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

mkdir -p "$GAMEDIR/samples"
$ESUDO chmod +x "$GAMEDIR/ardkore"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# Uncomment if A/B and X/Y feel swapped:
# export ARDKORE_SWAP_AB=1
# Raise (e.g. to 2048) if the audio crackles; lower for snappier response:
export ARDKORE_AUDIO_FRAMES=1024

./ardkore --fullscreen --samples "$GAMEDIR/samples" --project "$GAMEDIR/ardkore.prj"

pm_finish
