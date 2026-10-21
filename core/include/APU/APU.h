#pragma once

#include "IRQ.h"
#include "port/IAudioSink.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace allnes
{
namespace apu
{
using Byte    = std::uint8_t;
using Address = std::uint16_t;

// A countdown divider. The counter starts at zero, emits a tick when it
// reaches zero, and is then reloaded with the configured period.
class RateDivider
{
public:
    explicit RateDivider(int period = 0)
      : m_period(period)
    {
    }

    bool tick()
    {
        if (m_counter == 0)
        {
            m_counter = m_period;
            return true;
        }

        --m_counter;
        return false;
    }

    void setPeriod(int period) { m_period = period; }
    void restart() { m_counter = m_period; }
    int  period() const { return m_period; }

private:
    int m_period  = 0;
    int m_counter = 0;
};

// Units that follow the frame sequencer implement this interface. The
// sequencer addresses them only through these two callbacks.
class FrameClock
{
public:
    virtual ~FrameClock() = default;

    virtual void quarterFrame() {}
    virtual void halfFrame() {}
};

class LengthCounter : public FrameClock
{
public:
    void setEnabled(bool enabled);
    void load(std::size_t tableIndex);
    void halfFrame() override;

    bool muted() const { return !m_enabled || m_counter == 0; }
    int  value() const { return m_counter; }

    bool halt = false;

private:
    bool m_enabled = false;
    int  m_counter = 0;
};

class EnvelopeUnit : public FrameClock
{
public:
    void quarterFrame() override;

    int  level() const { return m_constant ? m_fixedVolume : m_decayLevel; }
    void trigger() { m_start = true; }

    void setFixedVolume(int volume) { m_fixedVolume = volume; }
    void setConstant(bool constant) { m_constant = constant; }
    void setLooping(bool looping) { m_looping = looping; }

    RateDivider divider { 0 };

private:
    int  m_fixedVolume = 15;
    int  m_decayLevel  = 15;
    bool m_constant    = true;
    bool m_looping     = false;
    bool m_start       = false;
};

class LinearCounter : public FrameClock
{
public:
    void setReloadValue(int value) { m_reloadValue = value; }
    void quarterFrame() override;

    bool reload  = false;
    bool control = true;

    int  value = 0;

private:
    int m_reloadValue = 0;
};

class PulseChannel;

class SweepUnit : public FrameClock
{
public:
    SweepUnit(PulseChannel& owner, bool onesComplement);

    void halfFrame() override;

    int  targetPeriod(int current) const;
    bool muted(int current, int target) const { return current < 8 || target > 0x7FF; }

    bool enabled = false;
    int  period  = 0;
    bool negate  = false;
    int  shift   = 0;
    bool reload  = false;

private:
    PulseChannel& m_owner;
    bool          m_onesComplement;
    RateDivider   m_divider { 0 };
};

class PulseChannel
{
public:
    enum Duty
    {
        Duty12_5 = 0,
        Duty25,
        Duty50,
        Duty25Negated,
    };

    explicit PulseChannel(bool firstChannel);

    void writeControl(Byte value);
    void writeSweep(Byte value);
    void writePeriodLow(Byte value);
    void writePeriodHigh(Byte value);

    void tick();
    Byte sample() const;

    int  period() const { return m_period; }
    void setPeriod(int period);

    static bool dutyActive(Duty duty, int step);

    EnvelopeUnit  envelope;
    LengthCounter length;
    SweepUnit     sweep;

private:
    Duty        m_duty      = Duty50;
    int         m_seqIndex  = 0;
    RateDivider m_sequencer { 0 };
    int         m_period    = 0;
};

class TriangleChannel
{
public:
    void writeLinearControl(Byte value);
    void writePeriodLow(Byte value);
    void writePeriodHigh(Byte value);

    // Clocked at the CPU rate.
    void tick();
    Byte sample() const;

    LengthCounter length;
    LinearCounter linear;

private:
    void setPeriod(int period);

    std::uint32_t m_seqIndex  = 0;
    RateDivider   m_sequencer { 0 };
    int           m_period    = 0;
};

class NoiseChannel
{
public:
    void writeControl(Byte value);
    void writePeriod(Byte value);
    void writeLength(Byte value);

    // Clocked at the CPU rate.
    void tick();
    Byte sample() const;

    EnvelopeUnit  envelope;
    LengthCounter length;

private:
    RateDivider m_divider { 0 };
    bool        m_shortMode    = false;
    std::uint16_t m_shiftRegister = 1;
};

class DmcChannel
{
public:
    DmcChannel(InterruptLine& irq, std::function<Byte(Address)> dma);

    void writeFrequency(Byte value);
    void writeOutput(Byte value);
    void writeSampleAddress(Byte value);
    void writeSampleLength(Byte value);
    void setEnabled(bool enabled);

    // Clocked at the CPU rate.
    void tick();
    Byte sample() const { return static_cast<Byte>(m_volume); }

    bool hasSamples() const { return m_remainingBytes > 0; }
    bool interruptFlag() const { return m_interrupt; }
    void clearInterrupt();

private:
    bool    loadSample();
    int     nextDelta();

    InterruptLine&                   m_irq;
    std::function<Byte(Address)> m_dma;

    bool    m_irqEnable      = false;
    bool    m_loop           = false;
    bool    m_enabled        = false;
    int     m_volume         = 0;
    RateDivider m_rateDivider { 0 };

    Address m_sampleBegin    = 0;
    int     m_sampleLength   = 0;
    int     m_remainingBytes = 0;
    Address m_currentAddress = 0;

    Byte    m_sampleBuffer   = 0;
    int     m_shifter        = 0;
    int     m_remainingBits  = 0;
    bool    m_silenced       = false;
    bool    m_interrupt      = false;
};

class FrameSequencer
{
public:
    enum Mode
    {
        FourStep = 0,
        FiveStep = 1,
    };

    FrameSequencer(std::vector<FrameClock*> units, InterruptLine& irq);

    void clock();
    void reset(Mode mode, bool irqInhibit);

    bool interruptFlag() const { return m_frameInterrupt; }
    void clearInterrupt();

private:
    // Positions within the 4-step and 5-step sequences, in APU samples.
    static constexpr int Q1 = 7457;
    static constexpr int Q2 = 14913;
    static constexpr int Q3 = 22371;
    static constexpr int Q4 = 29829;
    static constexpr int Q5 = 37281;
    static constexpr int FourStepLength = 29830;
    static constexpr int FiveStepLength = 37282;

    void quarterFrame();
    void halfFrame();

    std::vector<FrameClock*> m_units;
    InterruptLine&               m_irq;

    Mode m_mode             = FourStep;
    int  m_cycle            = 0;
    bool m_irqInhibit       = false;
    bool m_frameInterrupt   = false;
};
} // namespace apu

class APU
{
public:
    APU(IAudioSink& sink, InterruptLine& irq, std::function<std::uint8_t(std::uint16_t)> dmcDma);

    // Clocked once per CPU cycle.
    void step();

    void writeRegister(std::uint16_t address, std::uint8_t value);
    std::uint8_t readStatus();

    // One sample every two CPU clock periods.
    static int outputSampleRate();

    void flushAudio();

    void setMuted(bool muted) { m_muted = muted; }
    bool muted() const { return m_muted; }

    void reset();

private:
    static float mix(std::uint8_t pulse1, std::uint8_t pulse2, std::uint8_t triangle, std::uint8_t noise,
                     std::uint8_t dmc);

    apu::PulseChannel    m_pulse1;
    apu::PulseChannel    m_pulse2;
    apu::TriangleChannel m_triangle;
    apu::NoiseChannel    m_noise;
    apu::DmcChannel      m_dmc;
    apu::FrameSequencer  m_frameSequencer;

    IAudioSink&        m_sink;
    std::vector<float> m_samples;
    bool               m_secondHalf = false;
    bool               m_muted      = false;
};
} // namespace allnes
