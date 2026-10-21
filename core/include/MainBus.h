#pragma once

#include "APU/APU.h"
#include "Cartridge.h"
#include "Controller.h"
#include "Mapper.h"
#include "PPU.h"

#include <functional>
#include <vector>

namespace allnes
{
// The CPU's 64 KiB address space. Internal RAM and the PPU/APU/controller
// windows are decoded here; cartridge space is delegated to the mapper.
class MainBus
{
public:
    MainBus(PPU&              ppu,
            APU&              apu,
            Controller&       pad1,
            Controller&       pad2,
            std::function<void(Byte)> oamDma);

    Byte        read(Address address);
    void        write(Address address, Byte value);
    bool        setMapper(Mapper* mapper);
    const Byte* getPagePtr(Byte page);

private:
    // 2 KiB of work RAM, mirrored through $0000-$1FFF.
    std::vector<Byte> m_workRam { std::vector<Byte>(0x800) };

    // Optional 8 KiB battery/work RAM at $6000-$7FFF.
    std::vector<Byte> m_cartRam;

    std::function<void(Byte)> m_oamDma;

    Mapper*     m_mapper = nullptr;
    PPU&        m_ppu;
    APU&        m_apu;
    Controller& m_pad1;
    Controller& m_pad2;
};
} // namespace allnes
