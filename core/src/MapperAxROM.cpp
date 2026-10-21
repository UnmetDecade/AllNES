#include "MapperAxROM.h"
#include "Log.h"

namespace allnes
{
MapperAxROM::MapperAxROM(Cartridge& cartridge, std::function<void(void)> mirroringChanged)
  : Mapper(cartridge, Mapper::BoardAxrom)
  , m_mirroringChanged(std::move(mirroringChanged))
  , m_usesChrRam(cartridge.getVROM().empty())
{
    if (m_usesChrRam)
    {
        m_chrRam.resize(0x2000);
        LOG(Info) << "AxROM: using 8 KiB CHR-RAM" << std::endl;
    }
}

Byte MapperAxROM::readPRG(Address addr)
{
    const auto& rom   = m_cartridge.getROM();
    const auto  banks = static_cast<std::uint32_t>(rom.size() / 0x8000);
    const auto  bank  = banks ? (m_bank % banks) : 0u;

    return rom[bank * 0x8000 + (addr & 0x7FFF)];
}

void MapperAxROM::writePRG(Address, Byte value)
{
    m_bank          = value & 0x07;
    m_highNametable = (value & 0x10) != 0;
    m_mirroringChanged();
}

Byte MapperAxROM::readCHR(Address addr)
{
    if (addr >= 0x2000)
        return 0;

    if (m_usesChrRam)
        return m_chrRam[addr];

    const auto& chr = m_cartridge.getVROM();
    return chr.empty() ? 0xFF : chr[addr % chr.size()];
}

void MapperAxROM::writeCHR(Address addr, Byte value)
{
    if (addr < 0x2000 && m_usesChrRam)
        m_chrRam[addr] = value;
}

MirroringMode MapperAxROM::mirroringMode()
{
    return m_highNametable ? MirrorOneScreenHigher : MirrorOneScreenLower;
}

} // namespace allnes
