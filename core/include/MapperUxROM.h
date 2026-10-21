#pragma once

#include "Mapper.h"

#include <vector>

namespace allnes
{
// Mapper 2: switchable 16 KiB PRG bank at $8000, last bank fixed at $C000.
class MapperUxROM : public Mapper
{
public:
    explicit MapperUxROM(Cartridge& cartridge);

    Byte readPRG(Address addr) override;
    void writePRG(Address addr, Byte value) override;
    Byte readCHR(Address addr) override;
    void writeCHR(Address addr, Byte value) override;

private:
    std::uint32_t     m_bank      = 0;
    bool              m_usesChrRam = false;
    std::vector<Byte> m_chrRam;
};
} // namespace allnes
