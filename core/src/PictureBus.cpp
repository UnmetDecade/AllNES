#include "PictureBus.h"
#include "Log.h"

namespace allnes
{
Byte PictureBus::read(Address addr)
{
    addr &= 0x3FFF;

    if (addr < 0x2000)
        return m_mapper->readCHR(addr);

    if (addr < 0x3F00)
    {
        // $3000-$3EFF mirrors $2000-$2EFF; only the low 10 bits select a
        // nametable entry.
        const Address normalized = (addr >= 0x3000) ? static_cast<Address>(addr - 0x1000) : addr;
        const Address offset     = addr & 0x03FF;

        if (m_mapperOwnsNameTables)
            return m_mapper->readCHR(normalized);

        return m_vram[m_nameTableBase[(normalized >> 10) & 0x03] + offset];
    }

    return readPalette(addr & 0x1F);
}

void PictureBus::write(Address addr, Byte value)
{
    addr &= 0x3FFF;

    if (addr < 0x2000)
    {
        m_mapper->writeCHR(addr, value);
        return;
    }

    if (addr < 0x3F00)
    {
        const Address normalized = (addr >= 0x3000) ? static_cast<Address>(addr - 0x1000) : addr;
        const Address offset     = addr & 0x03FF;

        if (m_mapperOwnsNameTables)
        {
            m_mapper->writeCHR(normalized, value);
            return;
        }

        m_vram[m_nameTableBase[(normalized >> 10) & 0x03] + offset] = value;
        return;
    }

    Byte index = addr & 0x1F;
    if (index >= 0x10 && (index & 0x03) == 0)
        index &= 0x0F;
    m_palette[index] = value;
}

Byte PictureBus::readPalette(Byte paletteIndex)
{
    // $3F10/$3F14/$3F18/$3F1C mirror the background entries.
    if (paletteIndex >= 0x10 && (paletteIndex & 0x03) == 0)
        paletteIndex &= 0x0F;

    return m_palette[paletteIndex];
}

void PictureBus::updateMirroring()
{
    m_mapperOwnsNameTables = false;

    switch (m_mapper->mirroringMode())
    {
    case MirrorHorizontal:
        m_nameTableBase = { 0, 0, 0x400, 0x400 };
        break;
    case MirrorVertical:
        m_nameTableBase = { 0, 0x400, 0, 0x400 };
        break;
    case MirrorOneScreenLower:
        m_nameTableBase = { 0, 0, 0, 0 };
        break;
    case MirrorOneScreenHigher:
        m_nameTableBase = { 0x400, 0x400, 0x400, 0x400 };
        break;
    case MirrorFourScreen:
        m_mapperOwnsNameTables = true;
        break;
    default:
        m_nameTableBase = { 0, 0, 0, 0 };
        LOG(Error) << "Unsupported name-table mirroring: " << m_mapper->mirroringMode() << std::endl;
        break;
    }
}

bool PictureBus::setMapper(Mapper* mapper)
{
    if (!mapper)
    {
        LOG(Error) << "Mapper argument is nullptr" << std::endl;
        return false;
    }

    m_mapper = mapper;
    updateMirroring();
    return true;
}

void PictureBus::scanlineIRQ()
{
    m_mapper->scanlineIRQ();
}

} // namespace allnes
