#include "Emulator.h"
#include "Log.h"

#include <chrono>

namespace allnes
{
using std::chrono::high_resolution_clock;

Emulator::Emulator(IVideoSink& video, IAudioSink& audio, IInputSource& input)
  : m_video(video)
  , m_audio(audio)
  , m_input(input)
  , m_cpu(m_bus)
  , m_ppu(m_pictureBus)
  , m_apu(m_audio, m_cpu.createIRQHandler(), [this](Address addr) { return DMCDMA(addr); })
  , m_bus(m_ppu, m_apu, m_controller1, m_controller2, [this](Byte b) { OAMDMA(b); })
{
    m_ppu.setInterruptCallback([this]() { m_cpu.nmiInterrupt(); });
    m_controller1.connect(&m_input, 0);
    m_controller2.connect(&m_input, 1);
}

bool Emulator::loadRom(const std::string& path)
{
    m_loaded = false;

    if (!m_cartridge.loadFromFile(path))
        return false;

    m_mapper = Mapper::createMapper(static_cast<Mapper::Board>(m_cartridge.getMapper()),
                                    m_cartridge,
                                    m_cpu.createIRQHandler(),
                                    [this]() { m_pictureBus.updateMirroring(); });
    if (!m_mapper)
    {
        LOG(Error) << "Creating Mapper failed. Probably unsupported." << std::endl;
        return false;
    }

    if (!m_bus.setMapper(m_mapper.get()) || !m_pictureBus.setMapper(m_mapper.get()))
        return false;

    m_cpu.reset();
    m_ppu.reset();
    m_apu.reset();

    m_romPath = path;
    m_loaded  = true;
    return true;
}

void Emulator::reset()
{
    if (!m_loaded)
        return;

    m_cpu.reset();
    m_ppu.reset();
    m_apu.reset();
}

void Emulator::runFrame()
{
    if (!m_loaded)
        return;

    m_ppu.clearFrameReady();

    // One frame is ~29781 CPU steps in this timing model. The guard keeps a
    // misbehaving ROM from hanging the host forever.
    int guard = 0;
    while (!m_ppu.frameReady() && guard++ < 50000)
    {
        m_ppu.step();
        m_ppu.step();
        m_ppu.step();
        m_cpu.step();
        m_apu.step();
    }

    m_apu.flushAudio();
    m_video.present(m_ppu.frameBuffer(), NESVideoWidth, NESVideoHeight);
}

void Emulator::OAMDMA(Byte page)
{
    m_cpu.skipOAMDMACycles();
    auto page_ptr = m_bus.getPagePtr(page);
    if (page_ptr != nullptr)
    {
        m_ppu.doDMA(page_ptr);
    }
    else
    {
        LOG(Error) << "Can't get pageptr for DMA" << std::endl;
    }
}

Byte Emulator::DMCDMA(Address addr)
{
    m_cpu.skipDMCDMACycles();
    return m_bus.read(addr);
}

} // namespace allnes
