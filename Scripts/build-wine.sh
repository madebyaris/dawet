#!/bin/bash
# Build script for Wine 10.0 from CrossOver source
# Configured for macOS ARM64 (Apple Silicon)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
WINE_SRC="$PROJECT_ROOT/Libraries/wine"
BUILD_DIR="$PROJECT_ROOT/Libraries/wine/build-macos-arm64"
# Output to match DawetWineInstaller expected structure
# Install to Libraries/wine/install which will be copied to runtime location
INSTALL_PREFIX="$PROJECT_ROOT/Libraries/wine/install"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Building Wine 10.0 for macOS ARM64...${NC}"

# Check if source exists
if [ ! -d "$WINE_SRC" ]; then
    echo -e "${RED}Error: Wine source not found at $WINE_SRC${NC}"
    exit 1
fi

# Ensure we use Homebrew bison if available (Wine requires bison 3.0+)
# Homebrew bison is typically in /opt/homebrew/opt/bison/bin/ or /opt/homebrew/bin/
if [ -f "/opt/homebrew/opt/bison/bin/bison" ]; then
    export PATH="/opt/homebrew/opt/bison/bin:$PATH"
    echo -e "${YELLOW}Using Homebrew bison: $(bison --version | head -1)${NC}"
elif [ -f "/opt/homebrew/bin/bison" ]; then
    export PATH="/opt/homebrew/bin:$PATH"
    echo -e "${YELLOW}Using Homebrew bison: $(bison --version | head -1)${NC}"
fi

# Check for llvm-mingw (required for ARM64 Windows PE cross-compilation)
LLVM_MINGW_PATHS=(
    "/opt/homebrew/opt/llvm-mingw/bin"
    "/usr/local/opt/llvm-mingw/bin"
    "$HOME/llvm-mingw/bin"
    "/opt/llvm-mingw/bin"
)
LLVM_MINGW_FOUND=false
for LLVM_MINGW_PATH in "${LLVM_MINGW_PATHS[@]}"; do
    if [ -f "$LLVM_MINGW_PATH/aarch64-w64-mingw32-clang" ]; then
        export PATH="$LLVM_MINGW_PATH:$PATH"
        echo -e "${GREEN}Found llvm-mingw at: $LLVM_MINGW_PATH${NC}"
        LLVM_MINGW_FOUND=true
        break
    fi
done

# Check if cross-compiler is available
if ! command -v aarch64-w64-mingw32-clang &> /dev/null; then
    echo -e "${RED}Error: aarch64-w64-mingw32-clang not found.${NC}"
    echo -e "${YELLOW}Wine requires llvm-mingw for ARM64 Windows PE cross-compilation.${NC}"
    echo -e "${YELLOW}Run: ./install-llvm-mingw.sh${NC}"
    echo -e "${YELLOW}Or manually install from: https://github.com/mstorsjo/llvm-mingw/releases${NC}"
    exit 1
fi

# Create distversion.h (required by winedbg)
# This file is referenced by programs/winedbg/resource.h
if [ ! -f "$WINE_SRC/include/distversion.h" ]; then
    echo -e "${YELLOW}Creating distversion.h...${NC}"
    mkdir -p "$WINE_SRC/include"
    WINE_VERSION=$(cat "$WINE_SRC/VERSION" | grep -o '[0-9]\+\.[0-9]\+' | head -1)
    WINE_MAJOR=$(echo "$WINE_VERSION" | cut -d. -f1)
    WINE_MINOR=$(echo "$WINE_VERSION" | cut -d. -f2)
    cat > "$WINE_SRC/include/distversion.h" << EOF
#define WINDEBUG_MAJOR $WINE_MAJOR
#define WINDEBUG_MINOR $WINE_MINOR
#define WINDEBUG_BUILD 0
#define WINDEBUG_STR "$WINE_VERSION"
EOF
    # Also copy to programs/winedbg where resource.h includes it
    cp "$WINE_SRC/include/distversion.h" "$WINE_SRC/programs/winedbg/distversion.h"
fi

# Create build directory
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"

# Configure Wine for macOS ARM64
echo -e "${YELLOW}Configuring Wine...${NC}"
"$WINE_SRC/configure" \
    --prefix="$INSTALL_PREFIX" \
    --enable-win64 \
    --disable-win16 \
    --with-coreaudio \
    --without-alsa \
    --without-pulse \
    --with-freetype \
    --without-x \
    --without-wayland \
    --without-gphoto \
    --without-capi \
    --without-gettext \
    --disable-tests \
    --host=aarch64-apple-darwin \
    CFLAGS="-O2 -arch arm64" \
    CXXFLAGS="-O2 -arch arm64" \
    LDFLAGS="-arch arm64"

# Build Wine
echo -e "${YELLOW}Building Wine (this may take a while)...${NC}"
make -j$(sysctl -n hw.ncpu)

# Install to prefix
echo -e "${YELLOW}Installing Wine...${NC}"
make install

echo -e "${GREEN}Wine build completed successfully!${NC}"
echo -e "Binaries installed to: $INSTALL_PREFIX"

# Create version plist for DawetWineInstaller
# Note: Wine installs to $INSTALL_PREFIX, so binaries are at $INSTALL_PREFIX/bin
VERSION_PLIST="$INSTALL_PREFIX/DawetWineVersion.plist"
mkdir -p "$(dirname "$VERSION_PLIST")"
cat > "$VERSION_PLIST" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>version</key>
    <string>10.0.0</string>
</dict>
</plist>
EOF
echo -e "${GREEN}Version plist created at: $VERSION_PLIST${NC}"
echo -e "${YELLOW}Note: Set DAWET_SOURCE_ROOT=$PROJECT_ROOT environment variable for Xcode to find local build${NC}"

