#!/bin/bash
# Pictures of a fight in MUGEN (Windows) under Wine, on a virtual screen, to
# compare with Dolmexica. Needs wine, Xvfb running on :99 and ImageMagick:
#   Xvfb :99 -screen 0 1024x768x24 &
#   tools/mugen-shots.sh <mugen dir> <out dir> 60 3 kfm apocalypse -s kfm -p1.ai 4 -p2.ai 8
# In mugen.cfg: GameWidth 640, GameHeight 480, Sound 0.
# usage: mugen-shots.sh <mugen dir> <out dir> <seconds> <interval> <mugen args...>
M="$1"; OUT="$2"; SECS="$3"; INT="$4"; shift 4
export DISPLAY=:99 WINEDEBUG=-all
cd "$M" && wine mugen.exe "$@" > "$OUT/wine.log" 2>&1 &
WPID=$!
t=0
while [ "$t" -lt "$SECS" ]; do
  sleep "$INT"; t=$((t+INT))
  import -window root -display :99 "$OUT/t$(printf %03d $t).png" 2>/dev/null
done
wineserver -k 2>/dev/null
wait $WPID 2>/dev/null
echo done
