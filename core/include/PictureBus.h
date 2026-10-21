#pragma once

#include "Cartridge.h"
#include "Mapper.h"

#include <array>
#include <vector>

namespace allnes
{
// PPU address space: pattern tables come from the mapper, nametables from the
// 2 KiB VRAM (or the mapper for four-screen boards), and palettes from a small
// internal RAM array.
class PictureBus
{
public:
    Byte read(Address addr);
    void write(Address addr, Byte value);

    bool setMapper(Mapper* mapper);

    Byte readPalette(Byte paletteIndex);
    void updateMirroring();
    void scanlineIRQ();

private:
    // Base offset in m_vram for each of the four logical nametables.
    std::array<std::size_t, 4> m_nameTableBase { 0, 0, 0, 0 };

    // Four-screen boards keep their nametables on the cartridge.
    bool              m_mapperOwnsNameTables = false;
    Mapper*           m_mapper               = nullptr;
    std::vector<Byte> m_vram { std::vector<Byte>(0x800) };
    std::vector<Byte> m_palette { std::vector<Byte>(0x20) };
};
} // namespace allnes
