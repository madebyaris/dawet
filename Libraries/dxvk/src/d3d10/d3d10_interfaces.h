#pragma once

#include "d3d10_include.h"

#ifdef _MSC_VER
struct __declspec(uuid("0803425a-57f5-4dd6-9465-a87570834a08")) ID3D10StateBlock;
#else
// Guard against redefinition in newer mingw-w64 (13.0+) which already declares this UUID
#ifndef __MINGW64_VERSION_MAJOR
__CRT_UUID_DECL(ID3D10StateBlock, 0x0803425a,0x57f5,0x4dd6,0x94,0x65,0xa8,0x75,0x70,0x83,0x4a,0x08);
#endif
#endif
