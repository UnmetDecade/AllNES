#pragma once

#include "Mapper.h"

#include <vector>

namespace allnes
{
// Mapper 11 (Color Dreams): PRG bank in bits 0-1, CHR bank in bits 4-7.
class MapperColorDreams : public Mapper
{
public:
    MapperColorDreams(Cartridge& cartridge, std::function<void(void)> mirroringChanged);

    Byte readPRG(Address addr) override;
    void writePRG(Address addr, Byte value) override;
    Byte readCHR(Address addr) override;
    void writeCHR(Address addr, Byte value) override;
    MirroringMode mirroringMode() override;

private:
    std::function<void(void)> m_mirroringChanged;
    std::uint32_t             m_prgBank    = 0;
    std::uint32_t             m_chrBank    = 0;
    bool                      m_usesChrRam = false;
    std::vector<Byte>         m_chrRam;
};
} // namespace allnes
