#!/bin/bash
# Install llvm-mingw for Wine ARM64 Windows PE cross-compilation

set -e

INSTALL_DIR="$HOME/llvm-mingw"
LLVM_MINGW_URL="https://github.com/mstorsjo/llvm-mingw/releases/download/20251104/llvm-mingw-20251104-ucrt-macos-universal.tar.xz"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Installing llvm-mingw for Wine cross-compilation...${NC}"

# Check if already installed
if [ -f "$INSTALL_DIR/bin/aarch64-w64-mingw32-clang" ]; then
    echo -e "${YELLOW}llvm-mingw already installed at $INSTALL_DIR${NC}"
    echo -e "${GREEN}Add to PATH: export PATH=\"$INSTALL_DIR/bin:\$PATH\"${NC}"
    exit 0
fi

# Create install directory
mkdir -p "$INSTALL_DIR"
cd "$INSTALL_DIR"

# Download llvm-mingw
echo -e "${YELLOW}Downloading llvm-mingw (this may take a while)...${NC}"
curl -L -o llvm-mingw.tar.xz "$LLVM_MINGW_URL"

# Extract
echo -e "${YELLOW}Extracting llvm-mingw...${NC}"
tar -xf llvm-mingw.tar.xz --strip-components=1
rm llvm-mingw.tar.xz

# Verify installation
if [ -f "$INSTALL_DIR/bin/aarch64-w64-mingw32-clang" ]; then
    echo -e "${GREEN}llvm-mingw installed successfully!${NC}"
    echo -e "${GREEN}Location: $INSTALL_DIR${NC}"
    echo -e "${YELLOW}Add to PATH: export PATH=\"$INSTALL_DIR/bin:\$PATH\"${NC}"
    echo -e "${YELLOW}Or add to your ~/.zshrc: echo 'export PATH=\"\$HOME/llvm-mingw/bin:\$PATH\"' >> ~/.zshrc${NC}"
else
    echo -e "${RED}Installation failed!${NC}"
    exit 1
fi

