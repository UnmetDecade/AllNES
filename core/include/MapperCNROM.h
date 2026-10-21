#pragma once

#include "Mapper.h"

#include <vector>

namespace allnes
{
// Mapper 3: fixed PRG, 8 KiB CHR bank selected by a write to $8000-$FFFF.
class MapperCNROM : public Mapper
{
public:
    explicit MapperCNROM(Cartridge& cartridge);

    Byte readPRG(Address addr) override;
    void writePRG(Address addr, Byte value) override;
    Byte readCHR(Address addr) override;
    void writeCHR(Address addr, Byte value) override;

private:
    bool              m_mirroredPrg = false;
    std::uint32_t     m_chrBank     = 0;
    bool              m_usesChrRam  = false;
    std::vector<Byte> m_chrRam;
};
} // namespace allnes
