# CrossOver Components Integration

This directory contains the open-source components from CrossOver that have been integrated into Dawet.

## Components

- **Wine 10.0** - Windows compatibility layer (from CrossOver source)
- **DXVK 1.10.3** - Vulkan-based D3D9/D3D10/D3D11 implementation
- **MoltenVK** - Vulkan implementation on top of Metal (for macOS)
- **vkd3d** - Direct3D 12 to Vulkan translation layer
- **cabextract** - Microsoft Cabinet file extractor

## Building

### Prerequisites

- Xcode Command Line Tools
- Homebrew packages:
  ```bash
  brew install meson mingw-w64 glslang cmake
  ```

### Build All Components

```bash
cd Scripts
./build-all.sh
```

### Build Individual Components

```bash
# Build Wine
./Scripts/build-wine.sh

# Build DXVK
./Scripts/build-dxvk.sh

# Build MoltenVK
./Scripts/build-moltenvk.sh
```

## Build Output Structure

After building, components are installed to:

- Wine: `Libraries/wine/install/`
- DXVK: `Libraries/DXVK/x64/` and `Libraries/DXVK/x32/`
- MoltenVK: `Libraries/moltenvk/Package/`

## Using Local Builds

Dawet will automatically detect and use locally built components if they exist. The build system checks:

1. `DAWET_SOURCE_ROOT` environment variable
2. Relative to app bundle (for development builds)
3. Current working directory

To use local builds in Xcode, set the `DAWET_SOURCE_ROOT` environment variable to the project root directory.

## CrossOver Patches

All CrossOver-specific patches and optimizations are already included in the Wine source code. The CrossOver Wine 10.0 source includes:

- macOS-specific fixes and optimizations
- Apple Silicon (ARM64) support
- Performance improvements
- Compatibility enhancements

## Version Information

- Wine: 10.0.0
- DXVK: 1.10.3
- MoltenVK: Latest from CrossOver source

## Notes

- Building from source allows for custom optimizations and patches
- Local builds take precedence over downloaded binaries
- All components are built for macOS ARM64 (Apple Silicon)
- DXVK requires MoltenVK for macOS compatibility

