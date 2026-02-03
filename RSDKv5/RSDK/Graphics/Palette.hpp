#ifndef PALETTE_H
#define PALETTE_H

namespace RSDK
{

#define PALETTE_BANK_COUNT (0x8)
#define PALETTE_BANK_SIZE  (0x100)

union Color {
    uint8 bytes[4];
    uint32 color;
};

extern uint16 rgb32To16_R[0x100];
extern uint16 rgb32To16_G[0x100];
extern uint16 rgb32To16_B[0x100];

extern uint16 globalPalette[PALETTE_BANK_COUNT][PALETTE_BANK_SIZE];
extern uint16 activeGlobalRows[PALETTE_BANK_COUNT];
extern uint16 activeStageRows[PALETTE_BANK_COUNT];
extern uint16 stagePalette[PALETTE_BANK_COUNT][PALETTE_BANK_SIZE];

extern uint16 fullPalette[PALETTE_BANK_COUNT][PALETTE_BANK_SIZE];

extern uint8 gfxLineBuffer[SCREEN_YSIZE];

extern int32 maskColor;

#if RETRO_REV02
extern uint16 *tintLookupTable;
#else
extern uint16 tintLookupTable[0x10000];
#endif

#define RGB888_TO_BGR555(r, g, b) ((b) >> 3) | (((g) >> 3) << 5) | (((r) >> 3) << 10)
#define PACK_RGB888(r, g, b) RGB888_TO_BGR555(r, g, b)

#if RETRO_REV02
void LoadPalette(uint8 bankID, const char *filePath, uint16 disabledRows);
#endif

inline void SetActivePalette(uint8 newActiveBank, int32 startLine, int32 endLine)
{
    if (newActiveBank < PALETTE_BANK_COUNT)
        for (int32 l = startLine; l < endLine && l < SCREEN_YSIZE; l++) 
            gfxLineBuffer[l] = newActiveBank;
}

inline uint32 GetPaletteEntry(uint8 bankID, uint8 index)
{
    uint16 clr = fullPalette[bankID & 7][index];

    int32 B = (clr & 0x1F) << 3;         
    int32 G = ((clr >> 5) & 0x1F) << 3;   
    int32 R = ((clr >> 10) & 0x1F) << 3; 
    
    return (R << 16) | (G << 8) | B;
}

inline void SetPaletteEntry(uint8 bankID, uint8 index, uint32 color)
{
    fullPalette[bankID][index] = 
        ((color >> 0) & 0xFF) >> 3 |      
        (((color >> 8) & 0xFF) >> 3) << 5 |
        (((color >> 16) & 0xFF) >> 3) << 10;
}

inline void SetPaletteMask(uint32 color)
{
    maskColor = 
        ((color >> 0) & 0xFF) >> 3 |       
        (((color >> 8) & 0xFF) >> 3) << 5 |
        (((color >> 16) & 0xFF) >> 3) << 10;
}

#if RETRO_REV02
inline void SetTintLookupTable(uint16 *lookupTable) { tintLookupTable = lookupTable; }

#if RETRO_USE_MOD_LOADER && RETRO_MOD_LOADER_VER >= 2
inline uint16 *GetTintLookupTable() { return tintLookupTable; }
#endif

#else
inline uint16 *GetTintLookupTable() { return tintLookupTable; }
#endif

inline void CopyPalette(uint8 sourceBank, uint8 srcBankStart, uint8 destinationBank, uint8 destBankStart, uint8 count)
{
    if (sourceBank < PALETTE_BANK_COUNT && destinationBank < PALETTE_BANK_COUNT) {
        for (int32 i = 0; i < count; ++i) {
            fullPalette[destinationBank][destBankStart + i] = fullPalette[sourceBank][srcBankStart + i];
        }
    }
}

inline void RotatePalette(uint8 bankID, uint8 startIndex, uint8 endIndex, bool32 right)
{
    if (right) {
        uint16 startClr = fullPalette[bankID][endIndex];
        for (int32 i = endIndex; i > startIndex; --i) 
            fullPalette[bankID][i] = fullPalette[bankID][i - 1];
        fullPalette[bankID][startIndex] = startClr;
    }
    else {
        uint16 startClr = fullPalette[bankID][startIndex];
        for (int32 i = startIndex; i < endIndex; ++i) 
            fullPalette[bankID][i] = fullPalette[bankID][i + 1];
        fullPalette[bankID][endIndex] = startClr;
    }
}

#if RETRO_REV02
void BlendColors(uint8 destBankID, uint32 *srcColorsA, uint32 *srcColorsB, int32 blendAmount, int32 startIndex, int32 count);
#endif
void SetPaletteFade(uint8 destBankID, uint8 srcBankA, uint8 srcBankB, int16 blendAmount, int32 startIndex, int32 endIndex);

#if RETRO_REV0U
#include "Legacy/PaletteLegacy.hpp"
#endif

} // namespace RSDK

#endif