#pragma once

#include "Mapper.h"

#include <vector>

namespace allnes
{
// Mapper 1 (SxROM/MMC1): a five-write serial register controls PRG/CHR banking
// and nametable mirroring.
class MapperSxROM : public Mapper
{
public:
    MapperSxROM(Cartridge& cartridge, std::function<void(void)> mirroringChanged);

    Byte readPRG(Address addr) override;
    void writePRG(Address addr, Byte value) override;
    Byte readCHR(Address addr) override;
    void writeCHR(Address addr, Byte value) override;
    MirroringMode mirroringMode() override;

private:
    void updatePrgBanks();
    void updateChrBanks();

    std::function<void(void)> m_mirroringChanged;
    MirroringMode        m_mirroring = MirrorHorizontal;

    std::uint8_t m_shiftRegister = 0;
    int          m_writeCount    = 0;

    unsigned m_chrMode = 0;
    unsigned m_prgMode = 3;

    std::uint8_t m_prgRegister  = 0;
    std::uint8_t m_chrRegister0 = 0;
    std::uint8_t m_chrRegister1 = 0;

    std::size_t m_prgWindow0 = 0;
    std::size_t m_prgWindow1 = 0;
    std::size_t m_chrWindow0 = 0;
    std::size_t m_chrWindow1 = 0;

    bool              m_usesChrRam = false;
    std::vector<Byte> m_chrRam;
};
} // namespace allnes
