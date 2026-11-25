#!/usr/bin/env bash
# libs.sh — Add external static libraries here (sourced by build.sh)

# Fetch PBS-Payload PS4 backend sources if missing
if [ ! -d "$SRCDIR/common" ]; then
  echo "==> Fetching PBS-Payload PS4 capture backend..."
  git clone --depth 1 https://github.com/seregonwar/PBS-Payload.git "$WORKDIR/_pbs"
  mkdir -p "$SRCDIR"
  cp -r "$WORKDIR/_pbs/payload/src/"* "$SRCDIR/"
  rm -rf "$SRCDIR/platform/ps5" "$WORKDIR/_pbs"
fi

# Helper: build_lib <output_lib_file> <url> <folder_name> <build_commands>
build_lib() {
  local out="$SYSROOT/lib/$1"
  local url="$2"
  local dir="$3"
  local cmd="$4"

  [ -f "$out" ] && return 0

  echo "==> Building $dir..."
  cd "$SRCDIR"
  local archive="${url##*/}"
  [ -f "$archive" ] || wget -q "$url"
  rm -rf "$dir" && tar xf "$archive"
  cd "$dir"
  eval "$cmd"
  cd "$SRCDIR"
}

# --- Example Libraries (Add or remove as needed) ---

# build_lib "libopenh264.a" \
#   "https://github.com" \
#   "openh264-2.7.0" \
#   "make OS=linux ARCH=x86_64 CC='$CC' CXX='$CXX' AR='$AR' CFLAGS='-O2 -fPIC' CXXFLAGS='-O2 -fPIC' ENABLE64BIT=Yes ASM_ARCH=x86_64 USE_ASM=No libopenh264.a && cp libopenh264.a '$SYSROOT/lib/' && cp -r codec/api/wels '$SYSROOT/include/'"
