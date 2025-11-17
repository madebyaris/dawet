#!/bin/bash
# Build script for DXVK 1.10.3 from CrossOver source
# Configured for macOS with MoltenVK backend

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DXVK_SRC="$PROJECT_ROOT/Libraries/dxvk"
BUILD_DIR="$PROJECT_ROOT/Libraries/dxvk/build-macos"
INSTALL_DIR="$PROJECT_ROOT/Libraries/dxvk/install"
# Output structure should match: Libraries/DXVK/x64/ and Libraries/DXVK/x32/
DXVK_OUTPUT="$PROJECT_ROOT/Libraries/DXVK"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Building DXVK 1.10.3 for macOS...${NC}"

# Check if source exists
if [ ! -d "$DXVK_SRC" ]; then
    echo -e "${RED}Error: DXVK source not found at $DXVK_SRC${NC}"
    exit 1
fi

# Check for meson
if ! command -v meson &> /dev/null; then
    echo -e "${RED}Error: meson is required but not installed. Install with: brew install meson${NC}"
    exit 1
fi

# Check for mingw-w64
if ! command -v x86_64-w64-mingw32-gcc &> /dev/null && ! command -v i686-w64-mingw32-gcc &> /dev/null; then
    echo -e "${YELLOW}Warning: mingw-w64 not found. DXVK requires cross-compilation.${NC}"
    echo -e "${YELLOW}Install with: brew install mingw-w64${NC}"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure DXVK with meson
echo -e "${YELLOW}Configuring DXVK with meson...${NC}"
meson setup \
    --cross-file "$DXVK_SRC/build-win64.txt" \
    --prefix "$INSTALL_DIR" \
    --buildtype release \
    "$DXVK_SRC" \
    "$BUILD_DIR"

# Build DXVK
echo -e "${YELLOW}Building DXVK (this may take a while)...${NC}"
meson compile -C "$BUILD_DIR"

# Install
echo -e "${YELLOW}Installing DXVK...${NC}"
meson install -C "$BUILD_DIR"

echo -e "${GREEN}DXVK build completed successfully!${NC}"
echo -e "DLLs installed to: $INSTALL_DIR"

# Copy DLLs to expected structure for Dawet
# DXVK builds x64 and x32 DLLs that need to be in Libraries/DXVK/x64/ and Libraries/DXVK/x32/
mkdir -p "$DXVK_OUTPUT/x64"
mkdir -p "$DXVK_OUTPUT/x32"

# Find and copy DLLs from build directory
if [ -d "$BUILD_DIR/x64" ]; then
    find "$BUILD_DIR/x64" -name "*.dll" -exec cp {} "$DXVK_OUTPUT/x64/" \;
fi
if [ -d "$BUILD_DIR/x32" ]; then
    find "$BUILD_DIR/x32" -name "*.dll" -exec cp {} "$DXVK_OUTPUT/x32/" \;
fi

# Create version file
cat > "$DXVK_OUTPUT/version.txt" << EOF
1.10.3
EOF
echo -e "${GREEN}DXVK DLLs copied to: $DXVK_OUTPUT${NC}"

