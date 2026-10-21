#pragma once

#include "IRQ.h"
#include "Mapper.h"

#include <array>
#include <vector>

namespace allnes
{
// Mapper 4 (MMC3): eight 1 KiB CHR windows, four 8 KiB PRG windows, and a
// scanline counter that can assert the CPU IRQ.
class MapperMMC3 : public Mapper
{
public:
    MapperMMC3(Cartridge& cartridge, InterruptLine& irq, std::function<void(void)> mirroringChanged,
               Board board = BoardMmc3);

    Byte readPRG(Address addr) override;
    void writePRG(Address addr, Byte value) override;
    Byte readCHR(Address addr) override;
    void writeCHR(Address addr, Byte value) override;
    MirroringMode mirroringMode() override;

    void scanlineIRQ() override;

protected:
    // Extension hooks for MMC3-derived bootleg boards. The default is plain
    // MMC3 behaviour.
    virtual std::uint32_t prgBankNumber(std::uint32_t value) const { return value & 0x3F; }
    // Mapper 245's CHR-RAM carts ignore the CHR bank registers and expose the
    // whole 8 KiB linearly, which is the usual wiring for 8 KiB CHR-RAM boards.
    virtual bool linearChrRam() const { return false; }

    // Value of the most recent write to CHR bank registers 0-5 ($8001), used
    // by some bootleg boards to extend the PRG bank number.
    std::uint32_t m_lastChrBankWrite = 0;

    // Recompute the CHR and PRG windows from the current registers. Derived
    // boards call this from their constructor once their extension state is
    // set up.
    void refreshWindows();

private:
    void updateChrWindows();
    void updatePrgWindows();

    // Control registers. The reset values mirror the MMC3 power-on state
    // (R0=0, R1=2, R2=4, R3=5, R4=6, R5=7, R6=0, R7=1); several bootleg games
    // never write some of them and rely on these defaults.
    std::uint32_t m_bankSelect       = 0;
    bool          m_prgMode          = false;
    bool          m_chrMode          = false;
    std::array<std::uint32_t, 8> m_bankRegister { 0, 2, 4, 5, 6, 7, 0, 1 };

    // Scanline counter
    bool m_irqEnabled      = false;
    Byte m_irqCounter      = 0;
    Byte m_irqLatch        = 0;
    bool m_irqReloadPending = false;

    std::vector<Byte>           m_prgRam { std::vector<Byte>(0x2000) };
    std::vector<Byte>           m_nametableRam { std::vector<Byte>(0x1000) };
    std::array<std::size_t, 4>  m_prgWindows {};
    std::array<std::size_t, 8>  m_chrWindows {};

    bool              m_usesChrRam = false;
    std::vector<Byte> m_chrRam;

    MirroringMode        m_mirroring = MirrorHorizontal;
    std::function<void(void)> m_mirroringChanged;
    InterruptLine&                m_irq;
};

// Mapper 245: an MMC3 clone used by Chinese bootleg carts (the Dragon Quest
// ports among them). Bit 1 of the most recent CHR bank register write selects
// which 512 KiB half of PRG the switchable ($8000/$A000) windows address;
// this is what lets 1 MiB games such as Dragon Quest IV reach the second half.
// The hard-wired $C000/$E000 windows always stay at the top of the ROM.
class MapperMMC3_245 : public MapperMMC3
{
public:
    MapperMMC3_245(Cartridge& cartridge, InterruptLine& irq, std::function<void(void)> mirroringChanged);

protected:
    std::uint32_t prgBankNumber(std::uint32_t value) const override
    {
        // Low six bits select the bank; bit 1 of the last CHR write supplies
        // the 512 KiB half selector as PRG A19.
        return (value & 0x3F) | ((m_lastChrBankWrite & 0x02) << 5);
    }

    bool linearChrRam() const override { return true; }
};
} // namespace allnes
