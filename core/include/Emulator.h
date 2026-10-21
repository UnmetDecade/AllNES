#ifndef EMULATOR_H
#define EMULATOR_H

#include "APU/APU.h"
#include "CPU.h"
#include "Cartridge.h"
#include "Controller.h"
#include "MainBus.h"
#include "PPU.h"
#include "PictureBus.h"
#include "port/IAudioSink.h"
#include "port/IInputSource.h"
#include "port/IVideoSink.h"

#include <memory>
#include <string>

namespace allnes
{
const int NESVideoWidth  = ScanlineVisibleDots;
const int NESVideoHeight = VisibleScanlines;

// Platform-independent NES machine. The host owns the clock, the window and
// the event loop; this class exposes one emulated frame at a time and talks to
// the outside world only through the three ports.
class Emulator
{
public:
    Emulator(IVideoSink& video, IAudioSink& audio, IInputSource& input);

    // Returns false and logs the reason when the ROM cannot be loaded.
    bool loadRom(const std::string& path);

    // Reset the machine (soft reset) without reloading the ROM.
    void reset();

    // Run until the PPU has produced one complete frame, then present it and
    // flush the APU's audio samples to the sink.
    void runFrame();

    bool               isLoaded() const { return m_loaded; }
    const std::string& romPath() const { return m_romPath; }
    const Cartridge&   cartridge() const { return m_cartridge; }

    // Last completed frame (row-major 0xAARRGGBB). Valid after runFrame().
    const std::uint32_t* frameBuffer() const { return m_ppu.frameBuffer(); }

    // Current CPU program counter, for diagnostics and headless tooling.
    Address cpuPC() const { return m_cpu.getPC(); }

    void setMuted(bool muted) { m_apu.setMuted(muted); }

private:
    void OAMDMA(Byte page);
    Byte DMCDMA(Address addr);

    // Port references first so they are valid when the components below are
    // constructed.
    IVideoSink&   m_video;
    IAudioSink&   m_audio;
    IInputSource& m_input;

    CPU                     m_cpu;
    PictureBus              m_pictureBus;
    PPU                     m_ppu;
    APU                     m_apu;
    Cartridge               m_cartridge;
    std::unique_ptr<Mapper> m_mapper;

    Controller m_controller1, m_controller2;

    MainBus m_bus;

    bool        m_loaded = false;
    std::string m_romPath;
};
} // namespace allnes
#endif // EMULATOR_H
