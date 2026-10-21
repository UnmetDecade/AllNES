#include "MapperNROM.h"
#include "Log.h"

namespace allnes
{
MapperNROM::MapperNROM(Cartridge& cartridge)
  : Mapper(cartridge, Mapper::BoardNrom)
  , m_mirroredPrg(cartridge.getROM().size() == 0x4000)
  , m_usesChrRam(cartridge.getVROM().empty())
{
    if (m_usesChrRam)
    {
        m_chrRam.resize(0x2000);
        LOG(Info) << "NROM: using 8 KiB CHR-RAM" << std::endl;
    }
}

Byte MapperNROM::readPRG(Address addr)
{
    std::size_t offset = addr - 0x8000;
    if (m_mirroredPrg)
        offset &= 0x3FFF;

    return m_cartridge.getROM()[offset];
}

void MapperNROM::writePRG(Address addr, Byte value)
{
    LOG(InfoVerbose) << "NROM: ignored PRG write $" << std::hex << +addr << " = " << +value << std::endl;
}

Byte MapperNROM::readCHR(Address addr)
{
    if (m_usesChrRam)
        return m_chrRam[addr & 0x1FFF];

    const auto& chr = m_cartridge.getVROM();
    return chr[addr % chr.size()];
}

void MapperNROM::writeCHR(Address addr, Byte value)
{
    if (m_usesChrRam)
    {
        m_chrRam[addr & 0x1FFF] = value;
        return;
    }

    LOG(Info) << "NROM: write to read-only CHR at $" << std::hex << +addr << std::endl;
}

} // namespace allnes
