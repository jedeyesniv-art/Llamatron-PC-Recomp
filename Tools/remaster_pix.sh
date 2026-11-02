# Remaster pixel check (REMASTER.md "Verification"): the same scripted run twice, headless - A with capture
# (--mshot: the remaster's frame composed in software, --dump A.bin), B without (--shot, --dump B.bin) - then
# compares each frame. Run from the repo root in Git Bash; pass --remaster-ahead=0 among the input args.
# usage: Tools/remaster_pix.sh <name> <program> <frames> <shot frames...> -- <input args>
#   e.g. Tools/remaster_pix.sh gl GRIDRUNR.PRG 6000 3000 4000 -- --remaster-ahead=0 --press=600:mleft ...
# Output in $REMASTER_PIX_OUT/<name> (default logs/pix); RAM: python Tools/ramdiff.py <dir>/a.bin <dir>/b.bin
S="${REMASTER_PIX_OUT:-logs/pix}"
name=$1; prog=$2; frames=$3; shift 3
shots=(); while [ "$1" != "--" ]; do shots+=($1); shift; done; shift
rm -rf "$S/$name"; mkdir -p "$S/$name"
A=""; B=""
for f in "${shots[@]}"; do A="$A --mshot=$f:$S/$name/m$f.bmp"; B="$B --shot=$f:$S/$name/s$f.bmp"; done
./build/llamatron.exe --headless --program=$prog --userdata="$S/$name/uA" --log="$S/$name/a.log" --remaster-capture "$@" --frames=$frames $A --dump=$((frames-5)):"$S/$name/a.bin" >/dev/null &
./build/llamatron.exe --headless --program=$prog --userdata="$S/$name/uB" --log="$S/$name/b.log" "$@" --frames=$frames $B --dump=$((frames-5)):"$S/$name/b.bin" >/dev/null
wait
python -I - "$S/$name" "${shots[@]}" <<'PY'
import sys
from PIL import Image, ImageChops
d=sys.argv[1]
for f in sys.argv[2:]:
    a=Image.open(f'{d}/m{f}.bmp').convert('RGB'); b=Image.open(f'{d}/s{f}.bmp').convert('RGB')
    diff=ImageChops.difference(a,b); bb=diff.getbbox()
    n=sum(1 for p in diff.getdata() if p!=(0,0,0))
    print(f, 'identical' if not bb else f'{n} px differ, box {bb}')
PY
