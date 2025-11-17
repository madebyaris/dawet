#!/bin/bash
# Build script for MoltenVK from CrossOver source
# Configured for macOS ARM64

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MOLTENVK_SRC="$PROJECT_ROOT/Libraries/moltenvk"
BUILD_DIR="$PROJECT_ROOT/Libraries/moltenvk/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Building MoltenVK for macOS...${NC}"

# Check if source exists
if [ ! -d "$MOLTENVK_SRC" ]; then
    echo -e "${RED}Error: MoltenVK source not found at $MOLTENVK_SRC${NC}"
    exit 1
fi

# Check for Xcode
if ! command -v xcodebuild &> /dev/null; then
    echo -e "${RED}Error: Xcode command line tools required${NC}"
    exit 1
fi

cd "$MOLTENVK_SRC"

# Fetch dependencies if needed
if [ ! -d "External/SPIRV-Cross" ] || [ ! -d "External/glslang" ]; then
    echo -e "${YELLOW}Fetching MoltenVK dependencies...${NC}"
    ./fetchDependencies
fi

# Build dependencies for macOS first (required before building MoltenVK)
if [ ! -d "External/build/Latest/SPIRVCross.xcframework" ] || \
   [ ! -d "External/build/Latest/glslang.xcframework" ] || \
   [ ! -d "External/build/Latest/SPIRVTools.xcframework" ]; then
    echo -e "${YELLOW}Building MoltenVK dependencies for macOS (this may take a while)...${NC}"
    ./fetchDependencies --macos
fi

# Build using Makefile
echo -e "${YELLOW}Building MoltenVK (this may take a while)...${NC}"
make macos

# Package MoltenVK
echo -e "${YELLOW}Packaging MoltenVK...${NC}"
make package

echo -e "${GREEN}MoltenVK build completed successfully!${NC}"
echo -e "Framework available in: $MOLTENVK_SRC/Package"

