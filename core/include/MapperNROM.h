#pragma once

#include "Mapper.h"

#include <vector>

namespace allnes
{
// Mapper 0: no banking. 16 KiB PRG is mirrored, 32 KiB is linear.
class MapperNROM : public Mapper
{
public:
    explicit MapperNROM(Cartridge& cartridge);

    Byte readPRG(Address addr) override;
    void writePRG(Address addr, Byte value) override;
    Byte readCHR(Address addr) override;
    void writeCHR(Address addr, Byte value) override;

private:
    bool              m_mirroredPrg = false;
    bool              m_usesChrRam  = false;
    std::vector<Byte> m_chrRam;
};
} // namespace allnes
