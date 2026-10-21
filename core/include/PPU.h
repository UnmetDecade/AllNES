#pragma once

#include "PaletteColors.h"
#include "PictureBus.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace allnes
{
constexpr int ScanlineVisibleDots = 256;
constexpr int VisibleScanlines    = 240;

// Picture Processing Unit: the scanline/dot renderer, the 256-byte OAM (sprite)
// memory and the CPU-facing register block at $2000-$2007.
class PPU
{
public:
    explicit PPU(PictureBus& bus);

    void step();
    void reset();

    void setInterruptCallback(std::function<void(void)> cb);

    void doDMA(const Byte* page);

    // CPU-side register access.
    void control(Byte value);
    void setMask(Byte value);
    void setOAMAddress(Byte value);
    void setDataAddress(Byte value);
    void setScroll(Byte value);
    void setData(Byte value);

    Byte getStatus();
    Byte getData();
    Byte getOAMData();
    void setOAMData(Byte value);

    // Complete frame, row-major 0xAARRGGBB.
    const std::uint32_t* frameBuffer() const { return m_pixels.data(); }
    bool                 frameReady() const { return m_frameReady; }
    void                 clearFrameReady() { m_frameReady = false; }

private:
    enum Phase
    {
        PreRender,
        Visible,
        PostRender,
        VBlank,
    };

    enum PatternTable
    {
        TableLow = 0,
        TableHigh = 1,
    };

    static constexpr int DotsPerLine      = 341;
    static constexpr int LastDot          = 340;
    static constexpr int LastScanline     = 261;
    static constexpr int FirstVBlankLine  = 241;
    static constexpr int SpriteHeight     = 8;
    static constexpr int TallSpriteHeight = 16;

    // Either layer being enabled keeps the scroll registers and the mapper's
    // scanline counter running.
    bool rendering() const { return m_backgroundVisible || m_spritesVisible; }

    void tickPreRender();
    void tickVisible();
    void tickPostRender();
    void tickVBlank();

    void drawPixel(int x, int y);
    void incrementCoarseX();
    void incrementFineY();
    void restoreHorizontalBits();
    void restoreVerticalBits();
    void collectLineSprites();

    Byte readBus(Address addr) { return m_bus.read(addr); }

    PictureBus&               m_bus;
    std::function<void(void)> m_vblankInterrupt;

    // Sprite memory: four bytes per sprite (Y, tile, attributes, X).
    std::array<Byte, 256> m_oam {};
    std::vector<int>      m_lineSprites;

    Phase m_phase      = PreRender;
    int   m_dot        = 0;
    int   m_line       = 0;
    bool  m_evenFrame   = false;
    bool  m_frameReady = false;

    bool m_vblankFlag      = false;
    bool m_spriteZeroHit   = false;
    bool m_spriteOverflow  = false;

    // Loopy VRAM address (v), temporary address (t) and the write latch.
    Address m_vramAddr  = 0;
    Address m_tmpAddr   = 0;
    Byte    m_fineX     = 0;
    bool    m_addrLatch = false;
    Byte    m_readBuffer = 0;

    Byte m_oamAddr = 0;

    // PPUCTRL / PPUMASK state.
    bool         m_tallSprites       = false;
    bool         m_nmiEnabled        = false;
    bool         m_greyscale         = false;
    bool         m_spritesVisible    = false;
    bool         m_backgroundVisible = false;
    bool         m_clipSprites       = false;
    bool         m_clipBackground    = false;
    PatternTable m_bgTable           = TableLow;
    PatternTable m_sprTable          = TableLow;
    Address      m_vramStep          = 1;

    std::vector<std::uint32_t> m_pixels;
};
} // namespace allnes
