#include "MapperCNROM.h"
#include "Log.h"

namespace allnes
{
MapperCNROM::MapperCNROM(Cartridge& cartridge)
  : Mapper(cartridge, Mapper::BoardCnrom)
  , m_mirroredPrg(cartridge.getROM().size() == 0x4000)
  , m_usesChrRam(cartridge.getVROM().empty())
{
    if (m_usesChrRam)
    {
        m_chrRam.resize(0x2000);
        LOG(Info) << "CNROM: using 8 KiB CHR-RAM" << std::endl;
    }
}

Byte MapperCNROM::readPRG(Address addr)
{
    std::size_t offset = addr - 0x8000;
    if (m_mirroredPrg)
        offset &= 0x3FFF;

    return m_cartridge.getROM()[offset];
}

void MapperCNROM::writePRG(Address, Byte value)
{
    m_chrBank = value & 0x03;
}

Byte MapperCNROM::readCHR(Address addr)
{
    if (m_usesChrRam)
        return m_chrRam[addr & 0x1FFF];

    const auto& chr = m_cartridge.getVROM();
    return chr[(m_chrBank * 0x2000 + (addr & 0x1FFF)) % chr.size()];
}

void MapperCNROM::writeCHR(Address addr, Byte value)
{
    if (m_usesChrRam)
    {
        m_chrRam[addr & 0x1FFF] = value;
        return;
    }

    LOG(Info) << "CNROM: write to read-only CHR at $" << std::hex << +addr << std::endl;
}

} // namespace allnes
