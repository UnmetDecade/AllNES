#pragma once

#include "Cartridge.h"
#include "IRQ.h"

#include <functional>
#include <memory>

namespace allnes
{
// How the four logical name tables map onto the two physical ones.
enum MirroringMode
{
    MirrorHorizontal = 0,
    MirrorVertical   = 1,
    MirrorFourScreen = 8,
    MirrorOneScreenLower,
    MirrorOneScreenHigher,
};

// Cartridge board interface. A board translates CPU and PPU addresses into
// ROM/RAM accesses, selects mirroring, and may drive the scanline IRQ.
class Mapper
{
public:
    // iNES mapper numbers supported by the core.
    enum Board
    {
        BoardNrom        = 0,
        BoardSxrom       = 1,
        BoardUxrom       = 2,
        BoardCnrom       = 3,
        BoardMmc3        = 4,
        BoardAxrom       = 7,
        BoardColorDreams = 11,
        BoardGxrom       = 66,
        // MMC3-based Chinese bootleg board (Dragon Quest ports and others).
        BoardMmc3Bootleg245 = 245,
    };

    Mapper(Cartridge& cartridge, Board board)
      : m_cartridge(cartridge)
      , m_board(board)
    {
    }

    virtual ~Mapper() = default;

    virtual Byte readPRG(Address address)                  = 0;
    virtual void writePRG(Address address, Byte value)     = 0;
    virtual Byte readCHR(Address address)                  = 0;
    virtual void writeCHR(Address address, Byte value)     = 0;
    virtual MirroringMode mirroringMode();
    virtual void          scanlineIRQ() {}

    bool hasExtendedRAM() { return m_cartridge.hasExtendedRAM(); }

    static std::unique_ptr<Mapper> createMapper(Board                     board,
                                                Cartridge&                cartridge,
                                                InterruptLine&            irq,
                                                std::function<void(void)> mirroringChanged);

protected:
    Cartridge& m_cartridge;
    Board      m_board;
};
} // namespace allnes
