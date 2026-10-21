#include "PPU.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace allnes
{
PPU::PPU(PictureBus& bus)
  : m_bus(bus)
  , m_pixels(static_cast<std::size_t>(ScanlineVisibleDots) * VisibleScanlines, 0xFF000000u)
{
}

void PPU::reset()
{
    m_vblankFlag     = false;
    m_spriteZeroHit  = false;
    m_spriteOverflow = false;

    m_pixels.assign(static_cast<std::size_t>(ScanlineVisibleDots) * VisibleScanlines, 0xFF000000u);

    m_oam.fill(0);
    m_lineSprites.clear();
    m_lineSprites.reserve(8);

    m_phase      = PreRender;
    m_dot        = 0;
    m_line       = 0;
    m_evenFrame   = true;
    m_frameReady = false;

    m_vramAddr   = 0;
    m_tmpAddr    = 0;
    m_fineX      = 0;
    m_addrLatch  = true;
    m_readBuffer = 0;
    m_oamAddr    = 0;

    m_tallSprites       = false;
    m_nmiEnabled        = false;
    m_greyscale         = false;
    m_spritesVisible    = true;
    m_backgroundVisible = true;
    m_clipSprites       = false;
    m_clipBackground    = false;
    m_bgTable           = TableLow;
    m_sprTable          = TableLow;
    m_vramStep          = 1;
}

void PPU::setInterruptCallback(std::function<void(void)> cb)
{
    m_vblankInterrupt = std::move(cb);
}

void PPU::step()
{
    switch (m_phase)
    {
    case PreRender:
        tickPreRender();
        break;
    case Visible:
        tickVisible();
        break;
    case PostRender:
        tickPostRender();
        break;
    case VBlank:
        tickVBlank();
        break;
    }

    ++m_dot;
}

void PPU::tickPreRender()
{
    if (m_dot == 1)
    {
        // A new frame starts with the vblank flag and the sprite-zero latch
        // cleared.
        m_vblankFlag    = false;
        m_spriteZeroHit = false;
    }
    else if (m_dot == ScanlineVisibleDots + 2 && rendering())
    {
        restoreHorizontalBits();
    }
    else if (m_dot > 280 && m_dot <= 304 && rendering())
    {
        restoreVerticalBits();
    }

    // Odd frames with rendering enabled skip one dot.
    if (m_dot >= LastDot - ((!m_evenFrame && rendering()) ? 1 : 0))
    {
        m_phase = Visible;
        m_dot   = 0;
        m_line  = 0;
    }

    if (m_dot == 260 && rendering())
        m_bus.scanlineIRQ();
}

void PPU::tickVisible()
{
    if (m_dot > 0 && m_dot <= ScanlineVisibleDots)
    {
        drawPixel(m_dot - 1, m_line);
    }
    else if (m_dot == ScanlineVisibleDots + 1 && m_backgroundVisible)
    {
        incrementFineY();
    }
    else if (m_dot == ScanlineVisibleDots + 2 && rendering())
    {
        restoreHorizontalBits();
    }

    if (m_dot == 260 && rendering())
        m_bus.scanlineIRQ();

    if (m_dot >= LastDot)
    {
        collectLineSprites();
        ++m_line;
        m_dot = 0;
    }

    if (m_line >= VisibleScanlines)
        m_phase = PostRender;
}

void PPU::tickPostRender()
{
    if (m_dot >= LastDot)
    {
        ++m_line;
        m_dot        = 0;
        m_phase      = VBlank;
        m_frameReady = true;
    }
}

void PPU::tickVBlank()
{
    if (m_dot == 1 && m_line == FirstVBlankLine)
    {
        m_vblankFlag = true;
        if (m_nmiEnabled)
            m_vblankInterrupt();
    }

    if (m_dot >= LastDot)
    {
        ++m_line;
        m_dot = 0;
    }

    if (m_line >= LastScanline)
    {
        m_phase    = PreRender;
        m_line     = 0;
        m_evenFrame = !m_evenFrame;
    }
}

void PPU::drawPixel(int x, int y)
{
    unsigned bgColor   = 0;
    unsigned sprColor  = 0;
    bool     bgOpaque  = false;
    bool     sprOpaque = true;
    bool     sprInFront = false;

    if (m_backgroundVisible)
    {
        const int fineX = (m_fineX + x) % 8;

        if (!m_clipBackground || x >= 8)
        {
            const Address nametable = static_cast<Address>(0x2000 | (m_vramAddr & 0x0FFF));
            const Byte    tile      = readBus(nametable);

            // Each pattern row occupies 16 bytes: the low bit-plane is at
            // offset 0-7 and the high bit-plane at 8-15.
            Address pattern =
              static_cast<Address>(static_cast<int>(tile) * 16 + ((m_vramAddr >> 12) & 0x7));
            pattern |= static_cast<Address>(m_bgTable << 12);

            const int shift = 7 ^ fineX;
            bgColor         = (readBus(pattern) >> shift) & 0x1;
            bgColor |= ((readBus(pattern + 8) >> shift) & 0x1) << 1;

            bgOpaque = bgColor != 0;

            const Address attribute = static_cast<Address>(
              0x23C0 | (m_vramAddr & 0x0C00) | ((m_vramAddr >> 4) & 0x38) | ((m_vramAddr >> 2) & 0x07));
            const int attrShift = ((m_vramAddr >> 4) & 0x4) | (m_vramAddr & 0x2);
            bgColor |= static_cast<unsigned>((readBus(attribute) >> attrShift) & 0x3) << 2;

        }

        // The coarse X counter advances on every eighth dot even while the
        // left-edge mask hides the fetched pixels.
        if (fineX == 7)
            incrementCoarseX();
    }

    if (m_spritesVisible && (!m_clipSprites || x >= 8))
    {
        for (const int sprite : m_lineSprites)
        {
            const int spriteX = m_oam[sprite * 4 + 3];
            const int deltaX  = x - spriteX;

            if (deltaX < 0 || deltaX >= 8)
                continue;

            // OAM stores Y - 1, hence the increment.
            const Byte spriteY =
              static_cast<Byte>(static_cast<int>(m_oam[sprite * 4 + 0]) + 1);
            const Byte tile      = m_oam[sprite * 4 + 1];
            const Byte attribute = m_oam[sprite * 4 + 2];

            const int height = m_tallSprites ? TallSpriteHeight : SpriteHeight;

            int pixelX = deltaX % 8;
            int pixelY = (y - spriteY) % height;

            if ((attribute & 0x40) == 0)
                pixelX ^= 7;
            if (attribute & 0x80)
                pixelY ^= (height - 1);

            Address pattern = 0;

            if (!m_tallSprites)
            {
                pattern = static_cast<Address>(static_cast<int>(tile) * 16 + pixelY);
                if (m_sprTable == TableHigh)
                    pattern = static_cast<Address>(pattern + 0x1000);
            }
            else
            {
                // In 8x16 mode bit 0 of the tile selects the pattern table and
                // the two 8x8 halves are adjacent.
                const int halfOffset = (pixelY & 7) | ((pixelY & 8) << 1);
                pattern = static_cast<Address>((tile >> 1) * 32 + halfOffset);
                pattern |= static_cast<Address>((tile & 1) << 12);
            }

            sprColor = (readBus(pattern) >> pixelX) & 0x1;
            sprColor |= ((readBus(pattern + 8) >> pixelX) & 0x1) << 1;

            sprOpaque = sprColor != 0;
            if (!sprOpaque)
            {
                sprColor = 0;
                continue;
            }

            sprColor |= 0x10;                             // sprite palette
            sprColor |= (attribute & 0x3) << 2;           // palette bits
            sprInFront = (attribute & 0x20) == 0;

            if (!m_spriteZeroHit && m_backgroundVisible && sprite == 0 && bgOpaque)
                m_spriteZeroHit = true;

            break; // the first opaque sprite wins
        }
    }

    unsigned paletteIndex = bgColor;

    if ((!bgOpaque && sprOpaque) || (bgOpaque && sprOpaque && sprInFront))
        paletteIndex = sprColor;
    else if (!bgOpaque && !sprOpaque)
        paletteIndex = 0;

    m_pixels[static_cast<std::size_t>(y) * ScanlineVisibleDots + x] =
      paletteToArgb(m_bus.readPalette(static_cast<Byte>(paletteIndex)));
}

void PPU::incrementCoarseX()
{
    if ((m_vramAddr & 0x001F) == 31)
    {
        m_vramAddr &= static_cast<Address>(~0x001F);
        m_vramAddr ^= 0x0400;
    }
    else
    {
        ++m_vramAddr;
    }
}

void PPU::incrementFineY()
{
    if ((m_vramAddr & 0x7000) != 0x7000)
    {
        m_vramAddr = static_cast<Address>(m_vramAddr + 0x1000);
        return;
    }

    m_vramAddr &= static_cast<Address>(~0x7000);

    int coarseY = (m_vramAddr & 0x03E0) >> 5;
    if (coarseY == 29)
    {
        coarseY    = 0;
        m_vramAddr ^= 0x0800;
    }
    else if (coarseY == 31)
    {
        coarseY = 0;
    }
    else
    {
        ++coarseY;
    }

    m_vramAddr = static_cast<Address>((m_vramAddr & ~0x03E0) | (coarseY << 5));
}

void PPU::restoreHorizontalBits()
{
    m_vramAddr = static_cast<Address>((m_vramAddr & ~0x041F) | (m_tmpAddr & 0x041F));
}

void PPU::restoreVerticalBits()
{
    m_vramAddr = static_cast<Address>((m_vramAddr & ~0x7BE0) | (m_tmpAddr & 0x7BE0));
}

void PPU::collectLineSprites()
{
    m_lineSprites.clear();

    const int height = m_tallSprites ? TallSpriteHeight : SpriteHeight;
    int       count  = 0;

    for (int sprite = m_oamAddr / 4; sprite < 64; ++sprite)
    {
        const int delta = m_line - m_oam[sprite * 4];

        if (delta < 0 || delta >= height)
            continue;

        if (count >= 8)
        {
            m_spriteOverflow = true;
            break;
        }

        m_lineSprites.push_back(sprite);
        ++count;
    }
}

void PPU::doDMA(const Byte* page)
{
    // The transfer wraps around the end of OAM.
    const std::size_t first = 256 - m_oamAddr;
    std::memcpy(m_oam.data() + m_oamAddr, page, first);
    if (m_oamAddr != 0)
        std::memcpy(m_oam.data(), page + first, m_oamAddr);
}

void PPU::control(Byte value)
{
    m_nmiEnabled  = (value & 0x80) != 0;
    m_tallSprites = (value & 0x20) != 0;
    m_bgTable     = (value & 0x10) ? TableHigh : TableLow;
    m_sprTable    = (value & 0x08) ? TableHigh : TableLow;
    m_vramStep    = (value & 0x04) ? 0x20 : 1;

    m_tmpAddr = static_cast<Address>((m_tmpAddr & ~0x0C00) | ((value & 0x3) << 10));
}

void PPU::setMask(Byte value)
{
    m_greyscale         = (value & 0x01) != 0;
    m_clipBackground    = (value & 0x02) == 0;
    m_clipSprites       = (value & 0x04) == 0;
    m_backgroundVisible = (value & 0x08) != 0;
    m_spritesVisible    = (value & 0x10) != 0;
}

Byte PPU::getStatus()
{
    const Byte status = static_cast<Byte>((m_spriteOverflow ? 0x20 : 0x00) |
                                          (m_spriteZeroHit ? 0x40 : 0x00) |
                                          (m_vblankFlag ? 0x80 : 0x00));

    m_vblankFlag = false;
    m_addrLatch  = true;
    return status;
}

void PPU::setDataAddress(Byte value)
{
    if (m_addrLatch)
    {
        m_tmpAddr   = static_cast<Address>((m_tmpAddr & 0x00FF) | ((value & 0x3F) << 8));
        m_addrLatch = false;
    }
    else
    {
        m_tmpAddr  = static_cast<Address>((m_tmpAddr & 0xFF00) | value);
        m_vramAddr = m_tmpAddr;
        m_addrLatch = true;
    }
}

Byte PPU::getData()
{
    Byte value = readBus(m_vramAddr);
    m_vramAddr = static_cast<Address>(m_vramAddr + m_vramStep);

    // Outside the palette window the read is buffered: the value returned is
    // the previous buffer contents and the fetched byte replaces it.
    if (m_vramAddr < 0x3F00)
    {
        const Byte previous = m_readBuffer;
        m_readBuffer        = value;
        value               = previous;
    }

    return value;
}

void PPU::setData(Byte value)
{
    m_bus.write(m_vramAddr, value);
    m_vramAddr = static_cast<Address>(m_vramAddr + m_vramStep);
}

Byte PPU::getOAMData()
{
    return m_oam[m_oamAddr];
}

void PPU::setOAMAddress(Byte value)
{
    m_oamAddr = value;
}

void PPU::setOAMData(Byte value)
{
    m_oam[m_oamAddr++] = value;
}

void PPU::setScroll(Byte value)
{
    if (m_addrLatch)
    {
        m_tmpAddr  = static_cast<Address>((m_tmpAddr & ~0x001F) | ((value >> 3) & 0x1F));
        m_fineX    = value & 0x7;
        m_addrLatch = false;
    }
    else
    {
        m_tmpAddr = static_cast<Address>((m_tmpAddr & ~0x73E0) | ((value & 0x07) << 12) |
                                         ((value & 0xF8) << 2));
        m_addrLatch = true;
    }
}

} // namespace allnes
