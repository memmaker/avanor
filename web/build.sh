#!/bin/sh
# RVIP: build Avanor for the browser (Emscripten). Output in web/dist.
# Usage: sh web/build.sh            (release)
#        ASAN=1 sh web/build.sh     (emcc -fsanitize=address test build)
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
[ -n "$EMSDK" ] && export PATH="$EMSDK/upstream/emscripten:$PATH"
EXT=web/ext
OBJ=web/obj${ASAN:+-asan}
rm -rf web/dist
mkdir -p "$EXT" "$OBJ" web/dist

# Third-party sources, pinned; fetched once into web/ext (gitignored).
fetch() { [ -d "$EXT/$1" ] || git -c advice.detachedHead=false clone -q --depth 1 -b "$2" "$3" "$EXT/$1"; }
fetch lua51  v5.1.1 https://github.com/lua/lua.git
fetch sol2   v3.5.0 https://github.com/ThePhD/sol2.git
fetch fmt    11.0.2 https://github.com/fmtlib/fmt.git
fetch cereal v1.3.2 https://github.com/USCiLab/cereal.git
fetch zstd   v1.5.6 https://github.com/facebook/zstd.git
fetch argparse v3.2 https://github.com/p-ranav/argparse.git
[ -f external/stc.hpp ] || { mkdir -p external; curl -sSfL -o external/stc.hpp \
  https://raw.githubusercontent.com/illyigan/simple_term_colors/85c2194fe861d411c9790ea5362953ff5ca1bcd0/include/stc.hpp; }

OPT="-O2"
SAN=""
[ -n "$ASAN" ] && { OPT="-O1 -g"; SAN="-fsanitize=address"; }

# Lua 5.1 compiled as C++ so lua_error unwinds with exceptions (sol2 needs
# SOL_USING_CXX_LUA then). LuaJIT has no wasm target.
CXXFLAGS="$OPT $SAN -std=c++17 -fsigned-char -fexceptions -Wall -Wextra -I. -Iport \
 -isystem external -isystem $EXT/lua51 -isystem $EXT/sol2/include -isystem $EXT/fmt/include \
 -isystem $EXT/cereal/include -isystem $EXT/zstd/lib -isystem $EXT/argparse/include \
 -DSOL_ALL_SAFETIES_ON=1 -DSOL_USING_CXX_LUA=1 -DFMT_HEADER_ONLY=0"

LUASRC="lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib ldblib liolib lmathlib loslib ltablib lstrlib loadlib linit"
SRCS=$(sed -n '/^SRCS *=/,/^$/p' Makefile | sed 's/^SRCS *=//; s/\\//g')
VPATH="creature engine game helpers item lua magic map player ."

OBJS=""
for f in $LUASRC; do OBJS="$OBJS $OBJ/lua_$f.o"; done
for f in $SRCS; do
  for d in $VPATH; do [ -f "$d/$f" ] && { src="$d/$f"; break; }; done
  OBJS="$OBJS $OBJ/${f%.cpp}.o"
done
# c-rec reads the tile mapping (the remapper's rec file) at startup
CREC=${CREC:-$HOME/Projects/c-rec}
REC=${AVANOR_REC:-web/avanor-dawnlike.rec}
CXXFLAGS="$CXXFLAGS -I$CREC"
OBJS="$OBJS $OBJ/be_web.o $OBJ/rvip_tiles.o $OBJ/rec.o $OBJ/fmt_format.o $OBJ/fmt_os.o"
ZOBJS=""
for f in $EXT/zstd/lib/common/*.c $EXT/zstd/lib/compress/*.c $EXT/zstd/lib/decompress/*.c; do
  ZOBJS="$ZOBJS $OBJ/zstd_$(basename "${f%.c}").o"
done

NP=$(nproc 2>/dev/null || sysctl -n hw.ncpu)
{
  for f in $LUASRC; do echo "em++ -x c++ $OPT $SAN -fexceptions -w -DLUA_USE_POSIX -c $EXT/lua51/$f.c -o $OBJ/lua_$f.o"; done
  for f in $SRCS; do
    for d in $VPATH; do [ -f "$d/$f" ] && { src="$d/$f"; break; }; done
    echo "em++ -MMD $CXXFLAGS -c $src -o $OBJ/${f%.cpp}.o"
  done
  echo "em++ -MMD $CXXFLAGS -c port/be_web.cpp -o $OBJ/be_web.o"
  echo "em++ -MMD $CXXFLAGS -c port/rvip_tiles.cpp -o $OBJ/rvip_tiles.o"
  echo "emcc $OPT $SAN -c $CREC/rec.c -o $OBJ/rec.o"
  echo "em++ $OPT $SAN -std=c++17 -fexceptions -isystem $EXT/fmt/include -c $EXT/fmt/src/format.cc -o $OBJ/fmt_format.o"
  echo "em++ $OPT $SAN -std=c++17 -fexceptions -isystem $EXT/fmt/include -c $EXT/fmt/src/os.cc -o $OBJ/fmt_os.o"
  for f in $EXT/zstd/lib/common/*.c $EXT/zstd/lib/compress/*.c $EXT/zstd/lib/decompress/*.c; do
    echo "emcc $OPT $SAN -DZSTD_DISABLE_ASM -DZSTD_MULTITHREAD=0 -c $f -o $OBJ/zstd_$(basename "${f%.c}").o"
  done
} > "$OBJ/.all"

# Rebuild an object when its source (or, for game files, any header it
# listed in its .d) is newer than it.
: > "$OBJ/.todo"
while IFS= read -r cmd; do
  o=${cmd##* -o }
  d=${o%.o}.d
  if [ ! -f "$o" ]; then echo "$cmd" >> "$OBJ/.todo"; continue; fi
  deps=$(if [ -f "$d" ]; then sed 's/^[^:]*://; s/\\//g' "$d"; else echo "$cmd" | awk '{for(i=1;i<=NF;i++) if($i=="-c") print $(i+1)}'; fi)
  for dep in $deps; do [ "$dep" -nt "$o" ] && { echo "$cmd" >> "$OBJ/.todo"; break; }; done
done < "$OBJ/.all"
tr '\n' '\0' < "$OBJ/.todo" | xargs -0 -P "$NP" -n 1 sh -c 'echo "  ${0##* -c }"; $0'

em++ $OPT $SAN -fexceptions -o web/dist/avanor-core.js $OBJS $ZOBJS \
  -sASYNCIFY -sASYNCIFY_STACK_SIZE=262144 -sSTACK_SIZE=8388608 \
  -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=128MB \
  -sEXPORTED_FUNCTIONS=_main,_be_pushkey \
  -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,ENV,HEAPU8,HEAPU32,HEAP32,UTF8ToString,addRunDependency,removeRunDependency \
  -sFORCE_FILESYSTEM -lidbfs.js -sENVIRONMENT=web \
  --preload-file world@/world --preload-file manual@/manual --preload-file "$REC@/tiles.rec"
cp web/index.html web/dist/index.html
cp web/avanor.js web/dist/avanor.js
cp web/tiles-dawn.png web/dist/tiles-dawn.png
cp "$(dirname "$REC")/$(sed -n 's/^file: *//p' "$REC")" web/dist/   # the rec's sheet
python3 web/mksounds.py web/dist/sound
python3 web/make-help.py web/dist/help.html
echo "built web/dist"
