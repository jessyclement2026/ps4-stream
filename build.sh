#!/usr/bin/env bash
set -e

WORKDIR="$(cd "$(dirname "$0")" && pwd)"
SDK_DIR="${PS4_PAYLOAD_SDK:-$WORKDIR/ps4-payload-sdk}"

# 1. Download SDK only if it doesn't already exist
if [ ! -f "$SDK_DIR/toolchain/orbis.mk" ]; then
  echo "SDK not found — downloading ps4-payload-sdk into $SDK_DIR..."
  wget -q https://github.com/ps4-payload-dev/sdk/releases/latest/download/ps4-payload-sdk.zip
  unzip -oq ps4-payload-sdk.zip -d "$(dirname "$SDK_DIR")"
  rm -f ps4-payload-sdk.zip
fi

# 2. Export SDK & toolchain paths (works whether it was already there or just downloaded)
export WORKDIR
export PS4_PAYLOAD_SDK="$SDK_DIR"
export SYSROOT="$PS4_PAYLOAD_SDK/target"
export SRCDIR="$WORKDIR/src"

export CC="$PS4_PAYLOAD_SDK/bin/orbis-clang"
export CXX="$PS4_PAYLOAD_SDK/bin/orbis-clang++"
export AR="$PS4_PAYLOAD_SDK/bin/orbis-ar"
export RANLIB="$PS4_PAYLOAD_SDK/bin/orbis-ranlib"
export STRIP="$PS4_PAYLOAD_SDK/bin/orbis-strip"
export PKG_CONFIG="$PS4_PAYLOAD_SDK/bin/orbis-pkg-config"

export CFLAGS="-O2 -I$SYSROOT/include"
export LDFLAGS="-L$SYSROOT/lib"

# 3. Hook: Build external libraries if libs.sh exists
if [ -f "$WORKDIR/libs.sh" ]; then
  echo "Found libs.sh — building external dependencies..."
  mkdir -p "$SRCDIR"
  cd "$SRCDIR"
  source "$WORKDIR/libs.sh"
fi

# 4. Build final PS4 payload
cd "$WORKDIR"
make

echo ""
echo "=== BUILD COMPLETE ==="
