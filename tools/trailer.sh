#!/bin/sh
# Makes a video (a trailer) of the game from your own ROM:
#   tools/trailer.sh [output.mkv] [script]
# runs the mptest script (default tests/screens/trailer.txt) headless; its
# "record" commands write each scene's pictures and sound (see src/mptest.c),
# then ffmpeg cuts the scenes, adds the text and crossfades them into an .mkv:
# H.264 (yuv420p, 1920x1080, 60 fps; the game's pixels scaled up sharp) and
# FLAC audio. The sound is the game's own: the script can turn the music off
# (musicoff 1) and keep the sound effects.
#
# The edit is in the script, in lines that mptest skips as comments:
#   #> card|<seconds>|<title>|<subtitle>
#       a black card with text (no sound)
#   #> clip|<scene>|<start s>|<length s or ->|<caption|title|none>|<text>|<subtext>
#       part of a recorded scene; "caption" puts the text in the lower left,
#       "title" big in the middle
#   #> compare|<scene>|<scene 2>|<start s>|<length s or ->|<style>|<text>|<subtext>|<label>|<label 2>
#       two recordings of the same moment (e.g. without and with the 2D-HD
#       look) with a wipe moving from the right to the left between them
#   #> fade|<seconds>   crossfade between the parts (default 0.5)
# A scene is recorded with "record $WORK/<scene>" ... "record off"; $WORK is
# replaced by the work directory.
#
# Defaults: output $TRAILER_DIR/four-swords-trailer.mkv, where TRAILER_DIR is
# ${TMPDIR:-/tmp}/fsotp-trailer; the work directory is next to the output
# (TRAILER_WORK to change it). The raw frames never touch the disk (each scene's
# video.raw is a named pipe to an encoder; a lossless copy of each scene is
# kept until the end), and the work directory is removed afterwards
# (TRAILER_KEEP=1 keeps it). Needs the game built (make), zelda3_assets.dat,
# ffmpeg with libx264 and drawtext, and the DejaVu Sans font.
# Never commit what this makes: it is made from the ROM.
set -e
cd "$(dirname "$0")/.."
DIR=${TRAILER_DIR:-${TMPDIR:-/tmp}/fsotp-trailer}
OUT=${1:-$DIR/four-swords-trailer.mkv}
SCRIPT=${2:-tests/screens/trailer.txt}
mkdir -p "$(dirname "$OUT")"
OUT=$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")
WORK=${TRAILER_WORK:-$(dirname "$OUT")/trailer-work}
FONTDIR=${FONTDIR:-/usr/share/fonts/truetype/dejavu}
FONT=$FONTDIR/DejaVuSans.ttf
FONTB=$FONTDIR/DejaVuSans-Bold.ttf
[ -f "$FONT" ] && [ -f "$FONTB" ] || { echo "no DejaVu Sans in $FONTDIR (set FONTDIR)"; exit 1; }
[ -x ./zelda3 ] || { echo "build the game first (make)"; exit 1; }
RESAMPLE=aresample=48000
ffmpeg -hide_banner -buildconf 2>/dev/null | grep -q libsoxr && RESAMPLE=aresample=48000:resampler=soxr

rm -rf "$WORK"
mkdir -p "$WORK"
sed "s|\$WORK|$WORK|g" "$SCRIPT" > "$WORK/script.txt"
# (mptest reads lines of up to 255 characters: it gets the script without
# its comments and the edit.)
grep -v '^#' "$WORK/script.txt" > "$WORK/mptest.txt"

# 1. Record: an encoder per scene, waiting for its info.txt (the picture size).
pids=
for d in $(awk '$1 == "record" && $2 != "off" { print $2 }' "$WORK/script.txt" | sort -u); do
  mkdir -p "$d"
  mkfifo "$d/video.raw"
  (
    while [ ! -s "$d/info.txt" ]; do sleep 0.1; done
    w=$(awk '$1 == "width" { print $2 }' "$d/info.txt")
    h=$(awk '$1 == "height" { print $2 }' "$d/info.txt")
    ffmpeg -nostdin -v error -y -f rawvideo -pix_fmt bgr0 -s "${w}x${h}" -r 60 -i "$d/video.raw" \
      -c:v libx264rgb -qp 0 -preset ultrafast "$d/clip.mkv"
  ) &
  pids="$pids $!"
done
echo "recording (mptest log: $WORK/mptest.log)..."
status=0
./zelda3 --mptest "$WORK/mptest.txt" > "$WORK/mptest.log" 2>&1 || status=$?
# (Encoders of scenes that were never recorded still wait: let them see an
# empty input.)
for d in $(awk '$1 == "record" && $2 != "off" { print $2 }' "$WORK/script.txt" | sort -u); do
  if [ ! -s "$d/info.txt" ]; then
    printf 'width 2\nheight 2\n' > "$d/info.txt"
    : > "$d/video.raw"
  fi
done
for p in $pids; do
  wait "$p" || true
done
grep -E "FAIL|recorded" "$WORK/mptest.log" || true
if [ $status != 0 ]; then
  echo "the script failed (see $WORK/mptest.log)"
  exit 1
fi

# 2. The parts, each 1920x1080 60 fps with 48 kHz stereo sound.
FADE=$(awk -F'|' '$1 == "#> fade" { print $2 }' "$WORK/script.txt" | tail -1)
FADE=${FADE:-0.5}
cd "$WORK"
SCALE="scale=iw*4:ih*4:flags=neighbor,scale=1920:1080:flags=area,setsar=1,fps=60,format=yuv420p"
n=0
: > parts.txt
grep '^#> \(card\|clip\|compare\)|' script.txt | while IFS='|' read -r kind a b c d e f g h i; do
  part=$(printf 'part%02d' $n)
  case $kind in
  "#> card")
    secs=$a
    printf '%s' "$b" > $part.t1
    printf '%s' "$c" > $part.t2
    al="alpha='min(1,t/0.6)*min(1,($secs-t)/0.4)'"
    ffmpeg -nostdin -v error -y -f lavfi -i "color=c=black:s=1920x1080:r=60:d=$secs" \
      -f lavfi -t "$secs" -i "anullsrc=r=48000:cl=stereo" \
      -vf "drawtext=fontfile=$FONTB:textfile=$part.t1:fontcolor=white:fontsize=110:x=(w-tw)/2:y=(h-th)/2-60:$al,drawtext=fontfile=$FONT:textfile=$part.t2:fontcolor=0xd8c890:fontsize=46:x=(w-tw)/2:y=h/2+70:$al,format=yuv420p" \
      -c:v libx264 -crf 12 -preset medium -c:a pcm_s16le -shortest $part.mkv
    ;;
  "#> clip"|"#> compare")
    if [ "$kind" = "#> compare" ]; then
      # (the same fields after a second scene, then the two labels)
      scene=$a scene2=$b start=$c len=$d style=$e e=$f f=$g
      printf '%s' "$h" > $part.l1
      printf '%s' "$i" > $part.l2
    else
      scene=$a scene2= start=$b len=$c style=$d
    fi
    [ -f "$scene/clip.mkv" ] || { echo "no scene $scene"; exit 1; }
    frames=$(awk '$1 == "frames" { print $2 }' "$scene/info.txt")
    # (Not past the end of the scene.)
    avail=$(awk "BEGIN { print $frames / 60 - $start }")
    if [ "$len" = "-" ] || awk "BEGIN { exit !($len > $avail) }"; then
      [ "$len" = "-" ] || echo "note: $scene has only $avail s from $start s, not $len"
      len=$avail
    fi
    secs=$len
    printf '%s' "$e" > $part.t1
    printf '%s' "$f" > $part.t2
    al="alpha='min(1,max(0,(t-0.3)/0.5))*min(1,max(0,($secs-t-0.2)/0.4))'"
    case $style in
    caption)
      text="drawbox=x=0:y=ih-200:w=iw:h=150:color=black@0.5:t=fill:enable='between(t,0.2,$secs-0.1)',drawtext=fontfile=$FONTB:textfile=$part.t1:fontcolor=white:fontsize=56:x=80:y=h-188:$al,drawtext=fontfile=$FONT:textfile=$part.t2:fontcolor=0xd8c890:fontsize=34:x=82:y=h-112:$al" ;;
    title)
      text="drawbox=x=0:y=ih/2-150:w=iw:h=300:color=black@0.5:t=fill,drawtext=fontfile=$FONTB:textfile=$part.t1:fontcolor=white:fontsize=110:x=(w-tw)/2:y=h/2-110:borderw=4:bordercolor=black:$al,drawtext=fontfile=$FONT:textfile=$part.t2:fontcolor=0xf0e0a0:fontsize=46:x=(w-tw)/2:y=h/2+50:borderw=2:bordercolor=black:$al" ;;
    *)
      text=null ;;
    esac
    cut="trim=start=$start:duration=$len,setpts=PTS-STARTPTS,$SCALE,tpad=stop_mode=clone:stop_duration=1,trim=duration=$len"
    audio="[1:a]atrim=start=$start:duration=$len,asetpts=PTS-STARTPTS,$RESAMPLE,apad,atrim=duration=$len[a]"
    if [ -n "$scene2" ]; then
      # Two recordings of the same moment with a wipe going from the right
      # to the left between them (the first on the left), each with its
      # label; the first one's sound.
      [ -f "$scene2/clip.mkv" ] || { echo "no scene $scene2"; exit 1; }
      wipe="(0.8-0.6*min(1,max(0,(T-0.5)/($len-1))))"
      line="(0.8-0.6*min(1,max(0,(t-0.5)/($len-1))))"
      label="fontcolor=white:fontsize=44:y=40:borderw=3:bordercolor=black"
      video="[0:v]$cut,format=yuv444p[l];[2:v]$cut,format=yuv444p[r];[l][r]blend=all_expr='if(lt(X,W*$wipe),A,B)',format=yuv420p"
      video="$video,drawbox=x='iw*$line-2':y=0:w=4:h=ih:color=white@0.9:t=fill"
      video="$video,drawtext=fontfile=$FONTB:textfile=$part.l1:$label:x=40,drawtext=fontfile=$FONTB:textfile=$part.l2:$label:x=w-tw-40"
      ffmpeg -nostdin -v error -y -i "$scene/clip.mkv" -f s16le -ar 32040 -ac 2 -i "$scene/audio.raw" -i "$scene2/clip.mkv" \
        -filter_complex "$video,$text[v];$audio" \
        -map "[v]" -map "[a]" -c:v libx264 -crf 12 -preset medium -c:a pcm_s16le $part.mkv
    else
      ffmpeg -nostdin -v error -y -i "$scene/clip.mkv" -f s16le -ar 32040 -ac 2 -i "$scene/audio.raw" \
        -filter_complex "[0:v]$cut,$text[v];$audio" \
        -map "[v]" -map "[a]" -c:v libx264 -crf 12 -preset medium -c:a pcm_s16le $part.mkv
    fi
    ;;
  esac
  echo "$part.mkv $secs" >> parts.txt
  echo "made $part ($secs s)"
  n=$((n + 1))
done

# 3. Crossfade the parts together; the sound peaks at -1 dB.
inputs= vf= af= i=0 offset=0 lastv="0:v" lasta="0:a"
total=0
while read -r file secs; do
  inputs="$inputs -i $file"
  if [ $i -gt 0 ]; then
    offset=$(awk "BEGIN { print $total - $FADE }")
    vf="$vf[$lastv][$i:v]xfade=transition=fade:duration=$FADE:offset=$offset[v$i];"
    af="$af[$lasta][$i:a]acrossfade=d=$FADE[a$i];"
    lastv=v$i lasta=a$i
    total=$(awk "BEGIN { print $total + $secs - $FADE }")
  else
    total=$secs
  fi
  i=$((i + 1))
done < parts.txt
# shellcheck disable=SC2086
ffmpeg -nostdin -v error -y $inputs -filter_complex "${vf}${af}[$lastv]null[vo];[$lasta]anull[ao]" \
  -map "[vo]" -map "[ao]" -c:v libx264 -crf 18 -preset slow -pix_fmt yuv420p -r 60 -c:a pcm_s16le joined.mkv
peak=$(ffmpeg -nostdin -hide_banner -i joined.mkv -map 0:a -af volumedetect -f null - 2>&1 | awk '/max_volume/ { print $5 }')
gain=$(awk "BEGIN { g = -1 - ($peak); print (g > 0 ? g : 0) }")
ffmpeg -nostdin -v error -y -i joined.mkv -map 0 -c:v copy -af "volume=${gain}dB" -c:a flac \
  -metadata title="Four Swords from the Past" "$OUT"
cd - > /dev/null
[ -n "$TRAILER_KEEP" ] || rm -rf "$WORK"
echo "wrote $OUT ($(ffprobe -v error -show_entries format=duration -of csv=p=0 "$OUT") s, sound peak was $peak dB, raised $gain dB)"
