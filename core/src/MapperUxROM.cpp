#include "MapperUxROM.h"
#include "Log.h"

namespace allnes
{
MapperUxROM::MapperUxROM(Cartridge& cartridge)
  : Mapper(cartridge, Mapper::BoardUxrom)
  , m_usesChrRam(cartridge.getVROM().empty())
{
    if (m_usesChrRam)
    {
        m_chrRam.resize(0x2000);
        LOG(Info) << "UxROM: using 8 KiB CHR-RAM" << std::endl;
    }
}

Byte MapperUxROM::readPRG(Address addr)
{
    const auto& rom      = m_cartridge.getROM();
    const auto  bankSize = 0x4000u;
    const auto  banks    = static_cast<std::uint32_t>(rom.size() / bankSize);

    if (addr >= 0xC000)
        return rom[rom.size() - bankSize + (addr & 0x3FFF)];

    const auto bank = banks ? (m_bank % banks) : 0u;
    return rom[bank * bankSize + (addr & 0x3FFF)];
}

void MapperUxROM::writePRG(Address, Byte value)
{
    m_bank = value;
}

Byte MapperUxROM::readCHR(Address addr)
{
    if (m_usesChrRam)
        return m_chrRam[addr & 0x1FFF];

    const auto& chr = m_cartridge.getVROM();
    return chr[addr % chr.size()];
}

void MapperUxROM::writeCHR(Address addr, Byte value)
{
    if (m_usesChrRam)
    {
        m_chrRam[addr & 0x1FFF] = value;
        return;
    }

    LOG(Info) << "UxROM: write to read-only CHR at $" << std::hex << +addr << std::endl;
}

} // namespace allnes
