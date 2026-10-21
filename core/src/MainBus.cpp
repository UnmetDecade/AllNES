#include "MainBus.h"
#include "Log.h"

namespace allnes
{
namespace
{
// PPU registers are mirrored every eight bytes through $2000-$3FFF.
constexpr Address PpuControl   = 0x2000;
constexpr Address PpuMask      = 0x2001;
constexpr Address PpuStatus    = 0x2002;
constexpr Address OamAddress   = 0x2003;
constexpr Address OamData      = 0x2004;
constexpr Address PpuScroll    = 0x2005;
constexpr Address PpuAddress   = 0x2006;
constexpr Address PpuData      = 0x2007;

constexpr Address ApuFirst     = 0x4000;
constexpr Address ApuLast      = 0x4013;
constexpr Address OamDma       = 0x4014;
constexpr Address ApuStatus    = 0x4015;
constexpr Address Pad1Data     = 0x4016;
constexpr Address Pad2Data     = 0x4017;
} // namespace

MainBus::MainBus(PPU& ppu,
                 APU& apu,
                 Controller& player1,
                 Controller& player2,
                 std::function<void(Byte)> dmaCallback)
  : m_oamDma(std::move(dmaCallback))
  , m_ppu(ppu)
  , m_apu(apu)
  , m_pad1(player1)
  , m_pad2(player2)
{
}

Byte MainBus::read(Address addr)
{
    if (addr < 0x2000)
        return m_workRam[addr & 0x07FF];

    if (addr < 0x4020)
    {
        // $2008-$3FFF mirror $2000-$2007. The APU and I/O registers at
        // $4000-$401F are not mirrored and must keep their full address.
        const Address reg = (addr < 0x4000) ? static_cast<Address>(addr & 0x2007) : addr;
        switch (reg)
        {
        case PpuStatus:
            return m_ppu.getStatus();
        case PpuData:
            return m_ppu.getData();
        case OamData:
            return m_ppu.getOAMData();
        case Pad1Data:
            return m_pad1.read();
        case Pad2Data:
            return m_pad2.read();
        case ApuStatus:
            return m_apu.readStatus();
        default:
            LOG(InfoVerbose) << "Unmapped read at $" << std::hex << +addr << std::endl;
            return 0;
        }
    }

    if (addr < 0x6000)
    {
        LOG(InfoVerbose) << "Expansion ROM is not supported." << std::endl;
        return 0;
    }

    if (addr < 0x8000)
        return m_mapper->hasExtendedRAM() ? m_cartRam[addr - 0x6000] : 0;

    return m_mapper->readPRG(addr);
}

void MainBus::write(Address addr, Byte value)
{
    if (addr < 0x2000)
    {
        m_workRam[addr & 0x07FF] = value;
        return;
    }

    if (addr < 0x4020)
    {
        const Address reg = (addr < 0x4000) ? static_cast<Address>(addr & 0x2007) : addr;
        switch (reg)
        {
        case PpuControl:
            m_ppu.control(value);
            return;
        case PpuMask:
            m_ppu.setMask(value);
            return;
        case OamAddress:
            m_ppu.setOAMAddress(value);
            return;
        case OamData:
            m_ppu.setOAMData(value);
            return;
        case PpuScroll:
            m_ppu.setScroll(value);
            return;
        case PpuAddress:
            m_ppu.setDataAddress(value);
            return;
        case PpuData:
            m_ppu.setData(value);
            return;
        case OamDma:
            m_oamDma(value);
            return;
        case Pad1Data:
            m_pad1.strobe(value);
            m_pad2.strobe(value);
            return;
        case ApuStatus:
        case Pad2Data:
            m_apu.writeRegister(addr, value);
            return;
        default:
            if (addr >= ApuFirst && addr <= ApuLast)
            {
                m_apu.writeRegister(addr, value);
                return;
            }
            LOG(InfoVerbose) << "Unmapped write at $" << std::hex << +addr << std::endl;
            return;
        }
    }

    if (addr < 0x6000)
    {
        LOG(InfoVerbose) << "Expansion ROM is not supported." << std::endl;
        return;
    }

    if (addr < 0x8000)
    {
        if (m_mapper->hasExtendedRAM())
            m_cartRam[addr - 0x6000] = value;
        return;
    }

    m_mapper->writePRG(addr, value);
}

const Byte* MainBus::getPagePtr(Byte page)
{
    const Address addr = static_cast<Address>(page) << 8;

    if (addr < 0x2000)
        return m_workRam.data() + (addr & 0x07FF);

    if (addr < 0x6000)
    {
        LOG(Error) << "DMA from a register or expansion area is not supported: $" << std::hex << +addr << std::endl;
        return nullptr;
    }

    if (addr < 0x8000)
    {
        if (m_mapper->hasExtendedRAM())
            return m_cartRam.data() + (addr - 0x6000);
        return nullptr;
    }

    LOG(Error) << "DMA from PRG-ROM is not supported: page " << +page << std::endl;
    return nullptr;
}

bool MainBus::setMapper(Mapper* mapper)
{
    if (!mapper)
    {
        LOG(Error) << "Mapper pointer is nullptr" << std::endl;
        return false;
    }

    m_mapper = mapper;
    if (mapper->hasExtendedRAM())
        m_cartRam.resize(0x2000);

    return true;
}

} // namespace allnes
