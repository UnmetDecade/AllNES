#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace allnes
{
using Byte    = std::uint8_t;
using Address = std::uint16_t;

// An iNES cartridge image. The loader validates the header and exposes the
// PRG/CHR ROM plus the board information the mappers need.
class Cartridge
{
public:
    bool                     loadFromFile(std::string path);

    const std::vector<Byte>& getROM() const { return m_prgRom; }
    const std::vector<Byte>& getVROM() const { return m_chrRom; }
    Byte                     getMapper() const { return m_mapper; }
    Byte                     mirroringMode() const { return m_mirroring; }

    // The board-level PRG RAM window is always enabled; several commercial
    // dumps do not set the iNES flag even though the board provides it.
    bool hasExtendedRAM() const { return true; }

private:
    std::vector<Byte> m_prgRom;
    std::vector<Byte> m_chrRom;
    Byte              m_mapper     = 0;
    Byte              m_mirroring  = 0;
};
} // namespace allnes
