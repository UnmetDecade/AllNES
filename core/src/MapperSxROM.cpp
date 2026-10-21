#include "MapperSxROM.h"
#include "Log.h"

namespace allnes
{
namespace
{
std::size_t wrapOffset(std::size_t offset, std::size_t size)
{
    return size ? (offset % size) : 0;
}
} // namespace

MapperSxROM::MapperSxROM(Cartridge& cartridge, std::function<void(void)> mirroringChanged)
  : Mapper(cartridge, Mapper::BoardSxrom)
  , m_mirroringChanged(std::move(mirroringChanged))
  , m_usesChrRam(cartridge.getVROM().empty())
{
    if (m_usesChrRam)
    {
        m_chrRam.resize(0x8000);
        LOG(Info) << "SxROM: using 32 KiB CHR-RAM" << std::endl;
    }

    // Power-on state: first PRG window at $8000, last bank at $C000, and CHR
    // windows pointing at bank 0.
    m_prgWindow0 = 0;
    m_prgWindow1 = cartridge.getROM().size() - 0x4000;
    m_chrWindow0 = 0;
    m_chrWindow1 = 0;
}

Byte MapperSxROM::readPRG(Address addr)
{
    const auto& rom = m_cartridge.getROM();
    if (addr < 0xC000)
        return rom[wrapOffset(m_prgWindow0, rom.size()) + (addr & 0x3FFF)];

    return rom[wrapOffset(m_prgWindow1, rom.size()) + (addr & 0x3FFF)];
}

void MapperSxROM::writePRG(Address addr, Byte value)
{
    if (value & 0x80)
    {
        // Setting bit 7 resets the shift register and forces PRG mode 3.
        m_shiftRegister = 0;
        m_writeCount    = 0;
        m_prgMode       = 3;
        updatePrgBanks();
        return;
    }

    // The register is filled least-significant bit first.
    m_shiftRegister = static_cast<std::uint8_t>((m_shiftRegister >> 1) | ((value & 1) << 4));
    if (++m_writeCount != 5)
        return;

    const std::uint8_t data = m_shiftRegister;
    m_writeCount            = 0;
    m_shiftRegister         = 0;

    if (addr < 0xA000)
    {
        switch (data & 0x03)
        {
        case 0:
            m_mirroring = MirrorOneScreenLower;
            break;
        case 1:
            m_mirroring = MirrorOneScreenHigher;
            break;
        case 2:
            m_mirroring = MirrorVertical;
            break;
        default:
            m_mirroring = MirrorHorizontal;
            break;
        }
        m_mirroringChanged();

        m_chrMode = (data >> 4) & 1;
        m_prgMode = (data >> 2) & 0x03;
        updatePrgBanks();
        updateChrBanks();
        return;
    }

    if (addr < 0xC000)
    {
        m_chrRegister0 = data;
        updateChrBanks();
        return;
    }

    if (addr < 0xE000)
    {
        m_chrRegister1 = data;
        updateChrBanks();
        return;
    }

    m_prgRegister = data & 0x0F;
    updatePrgBanks();
}

void MapperSxROM::updatePrgBanks()
{
    const auto& rom  = m_cartridge.getROM();
    const auto  size = rom.size();

    switch (m_prgMode)
    {
    case 0:
    case 1:
        // 32 KiB window: two consecutive 16 KiB banks.
        m_prgWindow0 = static_cast<std::size_t>(m_prgRegister & ~1u) * 0x4000;
        m_prgWindow1 = m_prgWindow0 + 0x4000;
        break;
    case 2:
        // First bank fixed, second switchable.
        m_prgWindow0 = 0;
        m_prgWindow1 = static_cast<std::size_t>(m_prgRegister) * 0x4000;
        break;
    default:
        // First switchable, last bank fixed.
        m_prgWindow0 = static_cast<std::size_t>(m_prgRegister) * 0x4000;
        m_prgWindow1 = size >= 0x4000 ? size - 0x4000 : 0;
        break;
    }
}

void MapperSxROM::updateChrBanks()
{
    if (m_chrMode == 0)
    {
        // One 8 KiB bank. This core historically selects the odd 4 KiB half
        // first; keep that behaviour so existing save states/ROM output match.
        m_chrWindow0 = static_cast<std::size_t>(m_chrRegister0 | 1) * 0x1000;
        m_chrWindow1 = m_chrWindow0 + 0x1000;
    }
    else
    {
        m_chrWindow0 = static_cast<std::size_t>(m_chrRegister0) * 0x1000;
        m_chrWindow1 = static_cast<std::size_t>(m_chrRegister1) * 0x1000;
    }
}

MirroringMode MapperSxROM::mirroringMode()
{
    return m_mirroring;
}

Byte MapperSxROM::readCHR(Address addr)
{
    const std::size_t window = (addr < 0x1000) ? m_chrWindow0 : m_chrWindow1;
    const std::size_t offset = window + (addr & 0x0FFF);

    if (m_usesChrRam)
        return m_chrRam[offset % m_chrRam.size()];

    const auto& chr = m_cartridge.getVROM();
    return chr[offset % chr.size()];
}

void MapperSxROM::writeCHR(Address addr, Byte value)
{
    if (!m_usesChrRam)
    {
        LOG(Info) << "SxROM: write to read-only CHR at $" << std::hex << +addr << std::endl;
        return;
    }

    const std::size_t window = (addr < 0x1000) ? m_chrWindow0 : m_chrWindow1;
    m_chrRam[(window + (addr & 0x0FFF)) % m_chrRam.size()] = value;
}

} // namespace allnes
