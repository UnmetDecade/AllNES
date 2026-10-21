#pragma once

#include "Mapper.h"

#include <vector>

namespace allnes
{
// Mapper 66 (GxROM): one write selects a 32 KiB PRG bank (bits 4-5) and an
// 8 KiB CHR bank (bits 0-1).
class MapperGxROM : public Mapper
{
public:
    MapperGxROM(Cartridge& cartridge, std::function<void(void)> mirroringChanged);

    Byte readPRG(Address addr) override;
    void writePRG(Address addr, Byte value) override;
    Byte readCHR(Address addr) override;
    void writeCHR(Address addr, Byte value) override;
    MirroringMode mirroringMode() override;

private:
    std::function<void(void)> m_mirroringChanged;
    std::uint32_t             m_prgBank = 0;
    std::uint32_t             m_chrBank = 0;
    bool                      m_usesChrRam = false;
    std::vector<Byte>         m_chrRam;
};
} // namespace allnes
