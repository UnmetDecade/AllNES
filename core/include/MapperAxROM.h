#pragma once

#include "Mapper.h"

#include <vector>

namespace allnes
{
// Mapper 7: switchable 32 KiB PRG bank and single-screen mirroring selected by
// bits 0-2 and 4 of a write to $8000-$FFFF.
class MapperAxROM : public Mapper
{
public:
    MapperAxROM(Cartridge& cartridge, std::function<void(void)> mirroringChanged);

    Byte readPRG(Address addr) override;
    void writePRG(Address addr, Byte value) override;
    Byte readCHR(Address addr) override;
    void writeCHR(Address addr, Byte value) override;
    MirroringMode mirroringMode() override;

private:
    std::function<void(void)> m_mirroringChanged;
    std::uint32_t             m_bank         = 0;
    bool                      m_highNametable = false;
    bool                      m_usesChrRam    = false;
    std::vector<Byte>         m_chrRam;
};
} // namespace allnes
