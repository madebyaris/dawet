#!/bin/bash
# Master build script for all CrossOver components
# Builds Wine, DXVK, MoltenVK, and cabextract in the correct order

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  Building CrossOver Components for Dawet${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Build cabextract first (simplest, may be needed by others)
echo -e "${YELLOW}[1/4] Building cabextract...${NC}"
CABEXTRACT_SRC="$SCRIPT_DIR/../Libraries/cabextract"
CABEXTRACT_INSTALL="$CABEXTRACT_SRC/install"

# Remove install if it's a file instead of directory
if [ -f "$CABEXTRACT_INSTALL" ]; then
    rm -f "$CABEXTRACT_INSTALL"
fi

# Create install directory
mkdir -p "$CABEXTRACT_INSTALL"

cd "$CABEXTRACT_SRC"
if [ -f "configure" ]; then
    ./configure --prefix="$CABEXTRACT_INSTALL"
    make -j$(sysctl -n hw.ncpu)
    make install
else
    echo -e "${YELLOW}  cabextract already configured or using autotools${NC}"
fi
echo ""

# Build MoltenVK (needed by DXVK)
echo -e "${YELLOW}[2/4] Building MoltenVK...${NC}"
"$SCRIPT_DIR/build-moltenvk.sh"
echo ""

# Build DXVK (depends on MoltenVK)
echo -e "${YELLOW}[3/4] Building DXVK...${NC}"
"$SCRIPT_DIR/build-dxvk.sh"
echo ""

# Build Wine (main component)
echo -e "${YELLOW}[4/4] Building Wine...${NC}"
"$SCRIPT_DIR/build-wine.sh"
echo ""

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  All components built successfully!${NC}"
echo -e "${GREEN}========================================${NC}"

