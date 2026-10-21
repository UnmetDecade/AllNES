#include "Cartridge.h"
#include "Mapper.h"
#include "Log.h"

#include <array>
#include <cstring>
#include <fstream>

namespace allnes
{
namespace
{
constexpr std::size_t HeaderSize = 0x10;
constexpr std::size_t PrgBankSize = 0x4000;
constexpr std::size_t ChrBankSize = 0x2000;
constexpr std::array<unsigned char, 4> Magic { 'N', 'E', 'S', 0x1A };

bool readBlock(std::ifstream& file, std::vector<Byte>& out, std::size_t size)
{
    out.resize(size);
    return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size)));
}
} // namespace

bool Cartridge::loadFromFile(std::string path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        LOG(Error) << "Could not open ROM file from path: " << path << std::endl;
        return false;
    }

    std::array<Byte, HeaderSize> header {};
    if (!file.read(reinterpret_cast<char*>(header.data()), header.size()))
    {
        LOG(Error) << "Reading iNES header failed." << std::endl;
        return false;
    }

    if (std::memcmp(header.data(), Magic.data(), Magic.size()) != 0)
    {
        LOG(Error) << "Not a valid iNES image (bad magic number)." << std::endl;
        return false;
    }

    const unsigned prgBanks = header[4];
    const unsigned chrBanks = header[5];

    if (prgBanks == 0)
    {
        LOG(Error) << "ROM has no PRG-ROM banks. Loading ROM failed." << std::endl;
        return false;
    }

    // Flags 6: mirroring, battery, trainer, four-screen and the low nibble of
    // the mapper number. Flag 7 carries the high nibble.
    if (header[6] & 0x04)
    {
        LOG(Error) << "Trainer is not supported." << std::endl;
        return false;
    }

    m_mirroring = (header[6] & 0x08) ? static_cast<Byte>(MirroringMode::MirrorFourScreen)
                                     : static_cast<Byte>(header[6] & 0x01);
    m_mapper    = static_cast<Byte>(((header[6] >> 4) & 0x0F) | (header[7] & 0xF0));

    // iNES 1.0 byte 10: 0 = NTSC, 1 = PAL, 2 = dual. A number of old dumps
    // contain junk here, so only an explicit PAL flag is rejected.
    const Byte tvSystem = header[10] & 0x03;
    if (tvSystem == 0x01)
    {
        LOG(Error) << "PAL ROM not supported." << std::endl;
        return false;
    }

    LOG(Info) << "Mapper #" << +m_mapper << ", PRG banks " << prgBanks << ", CHR banks " << chrBanks << std::endl;

    if (!readBlock(file, m_prgRom, PrgBankSize * prgBanks))
    {
        LOG(Error) << "Reading PRG-ROM from image file failed." << std::endl;
        return false;
    }

    if (chrBanks != 0 && !readBlock(file, m_chrRom, ChrBankSize * chrBanks))
    {
        LOG(Error) << "Reading CHR-ROM from image file failed." << std::endl;
        return false;
    }

    if (chrBanks == 0)
        LOG(Info) << "Cartridge uses CHR-RAM." << std::endl;

    return true;
}

} // namespace allnes
