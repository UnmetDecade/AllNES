#include "MapperGxROM.h"
#include "Log.h"

namespace allnes
{
MapperGxROM::MapperGxROM(Cartridge& cartridge, std::function<void(void)> mirroringChanged)
  : Mapper(cartridge, Mapper::BoardGxrom)
  , m_mirroringChanged(std::move(mirroringChanged))
  , m_usesChrRam(cartridge.getVROM().empty())
{
    if (m_usesChrRam)
    {
        m_chrRam.resize(0x2000);
        LOG(Info) << "GxROM: using 8 KiB CHR-RAM" << std::endl;
    }
}

Byte MapperGxROM::readPRG(Address addr)
{
    const auto& rom = m_cartridge.getROM();
    const auto  offset = static_cast<std::size_t>(m_prgBank) * 0x8000 + (addr & 0x7FFF);
    return rom[offset % rom.size()];
}

void MapperGxROM::writePRG(Address, Byte value)
{
    m_prgBank = (value >> 4) & 0x03;
    m_chrBank = value & 0x03;
    // GxROM boards are wired for vertical mirroring.
    m_mirroringChanged();
}

Byte MapperGxROM::readCHR(Address addr)
{
    if (addr >= 0x2000)
        return 0;

    if (m_usesChrRam)
        return m_chrRam[addr];

    const auto& chr = m_cartridge.getVROM();
    return chr[(m_chrBank * 0x2000 + addr) % chr.size()];
}

void MapperGxROM::writeCHR(Address addr, Byte value)
{
    if (m_usesChrRam && addr < 0x2000)
        m_chrRam[addr] = value;
}

MirroringMode MapperGxROM::mirroringMode()
{
    return MirrorVertical;
}

} // namespace allnes
