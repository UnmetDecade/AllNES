#include "MapperMMC3.h"
#include "Log.h"

namespace allnes
{
MapperMMC3::MapperMMC3(Cartridge& cartridge, InterruptLine& irq, std::function<void(void)> mirroringChanged,
                       Board board)
  : Mapper(cartridge, board)
  , m_mirroringChanged(std::move(mirroringChanged))
  , m_irq(irq)
  , m_usesChrRam(cartridge.getVROM().empty())
{
    if (m_usesChrRam)
    {
        m_chrRam.resize(0x2000);
        LOG(Info) << "MMC3: using 8 KiB CHR-RAM" << std::endl;
    }

    // Apply the reset-value bank registers immediately: some games call into
    // a bank before ever touching the mapper registers.
    refreshWindows();
}

Byte MapperMMC3::readPRG(Address addr)
{
    if (addr >= 0x6000 && addr < 0x8000)
        return m_prgRam[addr & 0x1FFF];

    if (addr < 0x8000)
        return 0;

    const auto& rom = m_cartridge.getROM();
    const auto  slot = (addr >> 13) & 0x03;
    return rom[(m_prgWindows[slot] + (addr & 0x1FFF)) % rom.size()];
}

void MapperMMC3::writePRG(Address addr, Byte value)
{
    if (addr >= 0x6000 && addr < 0x8000)
    {
        m_prgRam[addr & 0x1FFF] = value;
        return;
    }

    if (addr < 0x8000)
        return;

    if (addr < 0xA000)
    {
        if ((addr & 1) == 0)
        {
            m_bankSelect = value & 0x07;
            m_prgMode    = (value & 0x40) != 0;
            m_chrMode    = (value & 0x80) != 0;

            // Flipping either mode bit reorders the windows even when no
            // bank register write follows.
            updateChrWindows();
            updatePrgWindows();
        }
        else
        {
            m_bankRegister[m_bankSelect] = value;
            if (m_bankSelect <= 5)
                m_lastChrBankWrite = value;
            updateChrWindows();
            updatePrgWindows();
        }
        return;
    }

    if (addr < 0xC000)
    {
        if ((addr & 1) == 0)
        {
            if (m_cartridge.mirroringMode() & 0x08)
                m_mirroring = MirrorFourScreen;
            else
                m_mirroring = (value & 0x01) ? MirrorHorizontal : MirrorVertical;

            m_mirroringChanged();
        }
        // Odd address: PRG RAM protect. Writes are always allowed here.
        return;
    }

    if (addr < 0xE000)
    {
        if ((addr & 1) == 0)
            m_irqLatch = value;
        else
        {
            m_irqCounter       = 0;
            m_irqReloadPending = true;
        }
        return;
    }

    m_irqEnabled = (addr & 1) != 0;
    m_irq.release();
}

void MapperMMC3::refreshWindows()
{
    updateChrWindows();
    updatePrgWindows();
}

void MapperMMC3::updateChrWindows()
{
    const auto& r = m_bankRegister;

    if (!m_chrMode)
    {
        m_chrWindows[0] = (r[0] & 0xFE) * 0x400;
        m_chrWindows[1] = m_chrWindows[0] + 0x400;
        m_chrWindows[2] = (r[1] & 0xFE) * 0x400;
        m_chrWindows[3] = m_chrWindows[2] + 0x400;
        m_chrWindows[4] = r[2] * 0x400;
        m_chrWindows[5] = r[3] * 0x400;
        m_chrWindows[6] = r[4] * 0x400;
        m_chrWindows[7] = r[5] * 0x400;
    }
    else
    {
        m_chrWindows[0] = r[2] * 0x400;
        m_chrWindows[1] = r[3] * 0x400;
        m_chrWindows[2] = r[4] * 0x400;
        m_chrWindows[3] = r[5] * 0x400;
        m_chrWindows[4] = (r[0] & 0xFE) * 0x400;
        m_chrWindows[5] = m_chrWindows[4] + 0x400;
        m_chrWindows[6] = (r[1] & 0xFE) * 0x400;
        m_chrWindows[7] = m_chrWindows[6] + 0x400;
    }
}

void MapperMMC3::updatePrgWindows()
{
    const auto& rom     = m_cartridge.getROM();
    const auto  romSize = rom.size();
    const auto  last16  = romSize - 0x4000;
    const auto  last8   = romSize - 0x2000;
    const auto  bankCount = romSize / 0x2000;

    auto bankOffset = [this, romSize, bankCount](std::uint32_t bank) {
        // The physical bank number is masked to the ROM size (MMC3 boards
        // simply ignore the high address lines). Only fall back to a modulo
        // for the unusual non-power-of-two ROM size.
        const auto mapped    = prgBankNumber(bank);
        const auto effective = (bankCount & (bankCount - 1)) == 0 ? (mapped & (bankCount - 1)) : (mapped % bankCount);
        return static_cast<std::size_t>(effective * 0x2000);
    };

    // The last two 8 KiB banks are hard-wired to the end of the ROM. Bootleg
    // boards such as mapper 245 OR their half-select bit into every window,
    // but these values already carry that bit (0xFE/0xFF), so the fixed
    // windows always stay in the upper half exactly as on real hardware.
    const std::size_t fixed16 = last16;
    const std::size_t fixed8  = last8;

    if (!m_prgMode)
    {
        m_prgWindows[0] = bankOffset(m_bankRegister[6]);
        m_prgWindows[1] = bankOffset(m_bankRegister[7]);
        m_prgWindows[2] = fixed16;
        m_prgWindows[3] = fixed8;
    }
    else
    {
        m_prgWindows[0] = fixed16;
        m_prgWindows[1] = bankOffset(m_bankRegister[7]);
        m_prgWindows[2] = bankOffset(m_bankRegister[6]);
        m_prgWindows[3] = fixed8;
    }
}

Byte MapperMMC3::readCHR(Address addr)
{
    if (addr < 0x2000)
    {
        if (m_usesChrRam && linearChrRam())
            return m_chrRam[addr & 0x1FFF];

        const auto bank   = m_chrWindows[(addr >> 10) & 0x07];
        const auto offset = bank + (addr & 0x03FF);

        if (m_usesChrRam)
            return m_chrRam[offset & 0x1FFF];

        const auto& chr = m_cartridge.getVROM();
        return chr[offset % chr.size()];
    }

    if (addr < 0x3000)
        return m_nametableRam[addr - 0x2000];

    return 0;
}

void MapperMMC3::writeCHR(Address addr, Byte value)
{
    if (addr < 0x2000)
    {
        if (m_usesChrRam)
        {
            if (linearChrRam())
            {
                m_chrRam[addr & 0x1FFF] = value;
                return;
            }
            const auto bank = m_chrWindows[(addr >> 10) & 0x07];
            m_chrRam[(bank + (addr & 0x03FF)) & 0x1FFF] = value;
        }
        return;
    }

    if (addr < 0x3000)
        m_nametableRam[addr - 0x2000] = value;
}

MirroringMode MapperMMC3::mirroringMode()
{
    return m_mirroring;
}

void MapperMMC3::scanlineIRQ()
{
    bool fire = false;

    if (m_irqCounter == 0 || m_irqReloadPending)
    {
        m_irqCounter       = m_irqLatch;
        m_irqReloadPending = false;
    }
    else
    {
        --m_irqCounter;
        fire = (m_irqCounter == 0);
    }

    if (fire && m_irqEnabled)
        m_irq.pull();
}

MapperMMC3_245::MapperMMC3_245(Cartridge& cartridge, InterruptLine& irq,
                               std::function<void(void)> mirroringChanged)
  : MapperMMC3(cartridge, irq, std::move(mirroringChanged), Mapper::BoardMmc3Bootleg245)
{
    // The reset sequence writes the default CHR registers 0,2,4,5,6,7 in
    // order, so the last CHR write latch starts at 7. Bit 1 is therefore set
    // at power-on and a 1 MiB cart boots from the upper 512 KiB half.
    m_lastChrBankWrite = 7;
    refreshWindows();
}

} // namespace allnes
