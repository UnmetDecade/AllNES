#include "APU/APU.h"

#include <algorithm>
#include <chrono>

namespace allnes
{
namespace
{
constexpr std::uint16_t RegPulse1Control = 0x4000;
constexpr std::uint16_t RegPulse1Sweep   = 0x4001;
constexpr std::uint16_t RegPulse1Low     = 0x4002;
constexpr std::uint16_t RegPulse1High    = 0x4003;

constexpr std::uint16_t RegPulse2Control = 0x4004;
constexpr std::uint16_t RegPulse2Sweep   = 0x4005;
constexpr std::uint16_t RegPulse2Low     = 0x4006;
constexpr std::uint16_t RegPulse2High    = 0x4007;

constexpr std::uint16_t RegTriangleLinear = 0x4008;
constexpr std::uint16_t RegTriangleLow    = 0x400A;
constexpr std::uint16_t RegTriangleHigh   = 0x400B;

constexpr std::uint16_t RegNoiseControl = 0x400C;
constexpr std::uint16_t RegNoisePeriod  = 0x400E;
constexpr std::uint16_t RegNoiseLength  = 0x400F;

constexpr std::uint16_t RegDmcFrequency = 0x4010;
constexpr std::uint16_t RegDmcOutput    = 0x4011;
constexpr std::uint16_t RegDmcAddress   = 0x4012;
constexpr std::uint16_t RegDmcLength    = 0x4013;

constexpr std::uint16_t RegStatus      = 0x4015;
constexpr std::uint16_t RegFrameControl = 0x4017;

// Length values indexed by the top five bits of $4003/$4007/$400B/$400F.
constexpr int LengthTable[32] = {
    10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
    12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};

// NTSC noise rate selectors.
constexpr int NoisePeriods[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068,
};

// NTSC DMC rate selectors (CPU cycles between output-level changes).
constexpr int DmcRates[16] = {
    428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54,
};
} // namespace

namespace apu
{
void LengthCounter::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!m_enabled)
        m_counter = 0;
}

void LengthCounter::load(std::size_t tableIndex)
{
    if (!m_enabled)
        return;

    m_counter = LengthTable[tableIndex];
}

void LengthCounter::halfFrame()
{
    if (halt || m_counter == 0)
        return;

    --m_counter;
}

void EnvelopeUnit::quarterFrame()
{
    if (m_start)
    {
        m_start      = false;
        m_decayLevel = 15;
        divider.setPeriod(m_fixedVolume);
        return;
    }

    if (!divider.tick())
        return;

    if (m_decayLevel > 0)
        --m_decayLevel;
    else if (m_looping)
        m_decayLevel = 15;
}

void LinearCounter::quarterFrame()
{
    if (reload)
    {
        value = m_reloadValue;
        if (!control)
            reload = false;
    }

    if (value == 0)
        return;

    --value;
}

SweepUnit::SweepUnit(PulseChannel& owner, bool onesComplement)
  : m_owner(owner)
  , m_onesComplement(onesComplement)
{
}

void SweepUnit::halfFrame()
{
    if (reload)
    {
        m_divider.setPeriod(period);
        reload = false;
        return;
    }

    if (!enabled)
        return;

    if (!m_divider.tick())
        return;

    if (shift == 0)
        return;

    const int current = m_owner.period();
    const int target  = targetPeriod(current);
    if (!muted(current, target))
        m_owner.setPeriod(target);
}

int SweepUnit::targetPeriod(int current) const
{
    const int amount = current >> shift;

    if (!negate)
        return current + amount;

    if (m_onesComplement)
        return std::max(0, current - amount - 1);

    return std::max(0, current - amount);
}

PulseChannel::PulseChannel(bool firstChannel)
  : sweep(*this, firstChannel)
{
}

void PulseChannel::setPeriod(int period)
{
    m_period = period;
    m_sequencer.setPeriod(period);
}

void PulseChannel::writeControl(Byte value)
{
    envelope.setFixedVolume(value & 0x0F);
    envelope.setConstant((value & 0x10) != 0);

    const bool loop = (value & 0x20) != 0;
    envelope.setLooping(loop);
    length.halt = loop;

    m_duty = static_cast<Duty>(value >> 6);
}

void PulseChannel::writeSweep(Byte value)
{
    sweep.enabled = (value & 0x80) != 0;
    sweep.period  = (value >> 4) & 0x07;
    sweep.negate  = (value & 0x08) != 0;
    sweep.shift   = value & 0x07;
    sweep.reload  = true;
}

void PulseChannel::writePeriodLow(Byte value)
{
    setPeriod((m_period & 0xFF00) | value);
}

void PulseChannel::writePeriodHigh(Byte value)
{
    setPeriod((m_period & 0x00FF) | ((value & 0x07) << 8));
    length.load(value >> 3);
    m_seqIndex = 0;
    envelope.trigger();
}

void PulseChannel::tick()
{
    if (m_sequencer.tick())
        m_seqIndex = (m_seqIndex + 7) & 0x07;
}

Byte PulseChannel::sample() const
{
    if (length.muted())
        return 0;

    if (sweep.muted(m_period, sweep.targetPeriod(m_period)))
        return 0;

    if (!dutyActive(m_duty, m_seqIndex))
        return 0;

    return static_cast<Byte>(envelope.level());
}

bool PulseChannel::dutyActive(Duty duty, int step)
{
    switch (duty)
    {
    case Duty12_5:
        return step == 7;
    case Duty25:
        return step >= 6;
    case Duty50:
        return step >= 4;
    case Duty25Negated:
        return step < 6;
    }

    return false;
}

void TriangleChannel::setPeriod(int period)
{
    m_period = period;
    m_sequencer.setPeriod(period);
}

void TriangleChannel::writeLinearControl(Byte value)
{
    linear.setReloadValue(value & 0x7F);
    linear.reload = true;

    const bool control = (value >> 7) != 0;
    linear.control     = control;
    length.halt        = control;
}

void TriangleChannel::writePeriodLow(Byte value)
{
    setPeriod((m_period & 0xFF00) | value);
}

void TriangleChannel::writePeriodHigh(Byte value)
{
    setPeriod((m_period & 0x00FF) | ((value & 0x07) << 8));
    length.load(value >> 3);
    linear.reload = true;
}

void TriangleChannel::tick()
{
    if (length.muted())
        return;

    if (linear.value == 0)
        return;

    if (m_sequencer.tick())
        m_seqIndex = (m_seqIndex + 1) & 0x1F;
}

Byte TriangleChannel::sample() const
{
    if (m_seqIndex < 16)
        return static_cast<Byte>(15 - m_seqIndex);

    return static_cast<Byte>(m_seqIndex - 16);
}

void NoiseChannel::writeControl(Byte value)
{
    envelope.setFixedVolume(value & 0x0F);
    envelope.setConstant((value & 0x10) != 0);

    const bool loop = (value & 0x20) != 0;
    envelope.setLooping(loop);
    length.halt = loop;
}

void NoiseChannel::writePeriod(Byte value)
{
    m_shortMode = (value & 0x80) != 0;
    m_divider.setPeriod(NoisePeriods[value & 0x0F]);
}

void NoiseChannel::writeLength(Byte value)
{
    length.load(value >> 3);
    envelope.trigger();
}

void NoiseChannel::tick()
{
    if (!m_divider.tick())
        return;

    // Noise mode: bit 0 XOR bit 1 (long mode) or bit 6 (short mode).
    const std::uint16_t tapBit = m_shortMode ? 0x0040 : 0x0002;
    const bool feedback = ((m_shiftRegister & 0x0001) != 0) !=
                          ((m_shiftRegister & tapBit) != 0);

    m_shiftRegister =
      static_cast<std::uint16_t>((m_shiftRegister >> 1) | (feedback ? 0x4000 : 0x0000));
}

Byte NoiseChannel::sample() const
{
    // The shift register's LSB gates the envelope: a set bit silences the
    // channel for that step. Without this the channel would only emit DC.
    if (length.muted() || (m_shiftRegister & 0x0001) != 0)
        return 0;

    return static_cast<Byte>(envelope.level());
}

DmcChannel::DmcChannel(InterruptLine& irq, std::function<Byte(Address)> dma)
  : m_irq(irq)
  , m_dma(std::move(dma))
{
}

void DmcChannel::writeFrequency(Byte value)
{
    m_irqEnable = (value >> 7) != 0;
    m_loop      = (value >> 6) != 0;

    m_rateDivider.setPeriod(DmcRates[value & 0x0F]);
    m_rateDivider.restart();
}

void DmcChannel::writeOutput(Byte value)
{
    m_volume = value & 0x7F;
}

void DmcChannel::writeSampleAddress(Byte value)
{
    m_sampleBegin = static_cast<Address>(0xC000 | (value << 6));
}

void DmcChannel::writeSampleLength(Byte value)
{
    m_sampleLength = (value << 4) | 1;
}

void DmcChannel::setEnabled(bool enabled)
{
    m_enabled = enabled;

    if (!enabled)
    {
        m_remainingBytes = 0;
    }
    else if (m_remainingBytes == 0)
    {
        m_currentAddress = m_sampleBegin;
        m_remainingBytes = m_sampleLength;
    }
}

void DmcChannel::clearInterrupt()
{
    m_irq.release();
    m_interrupt = false;
}

void DmcChannel::tick()
{
    if (!m_enabled)
        return;

    if (!m_rateDivider.tick())
        return;

    const int delta = nextDelta();
    if (m_silenced)
        return;

    if (delta == 1 && m_volume <= 125)
        m_volume += 2;
    else if (delta == 0 && m_volume >= 2)
        m_volume -= 2;
}

int DmcChannel::nextDelta()
{
    if (m_remainingBits == 0)
    {
        m_remainingBits = 8;

        if (loadSample())
        {
            m_shifter  = m_sampleBuffer;
            m_silenced = false;
        }
        else
        {
            m_silenced = true;
        }
    }

    // Each loaded byte provides exactly eight output bits, including the bit
    // shifted out in the load cycle.
    --m_remainingBits;

    const int delta = m_shifter & 0x01;
    m_shifter >>= 1;
    return delta;
}

bool DmcChannel::loadSample()
{
    if (m_remainingBytes == 0)
    {
        if (!m_loop)
        {
            if (m_irqEnable)
            {
                m_interrupt = true;
                m_irq.pull();
            }

            return false;
        }

        m_currentAddress = m_sampleBegin;
        m_remainingBytes = m_sampleLength;
    }
    else
    {
        --m_remainingBytes;
    }

    m_sampleBuffer = m_dma(m_currentAddress);

    if (m_currentAddress == 0xFFFF)
        m_currentAddress = 0x8000;
    else
        ++m_currentAddress;

    return true;
}

FrameSequencer::FrameSequencer(std::vector<FrameClock*> units, InterruptLine& irq)
  : m_units(std::move(units))
  , m_irq(irq)
{
}

void FrameSequencer::quarterFrame()
{
    for (FrameClock* unit : m_units)
        unit->quarterFrame();
}

void FrameSequencer::halfFrame()
{
    for (FrameClock* unit : m_units)
        unit->halfFrame();
}

void FrameSequencer::clock()
{
    ++m_cycle;

    switch (m_cycle)
    {
    case Q1:
        quarterFrame();
        break;
    case Q2:
        quarterFrame();
        halfFrame();
        break;
    case Q3:
        quarterFrame();
        break;
    case Q4:
        if (m_mode == FourStep)
        {
            quarterFrame();
            halfFrame();

            if (!m_irqInhibit)
            {
                m_irq.pull();
                m_frameInterrupt = true;
            }
        }
        break;
    case Q5:
        if (m_mode == FiveStep)
        {
            quarterFrame();
            halfFrame();
        }
        break;
    default:
        break;
    }

    if ((m_mode == FourStep && m_cycle == FourStepLength) || m_cycle == FiveStepLength)
        m_cycle = 0;
}

void FrameSequencer::reset(Mode mode, bool irqInhibit)
{
    m_mode       = mode;
    m_irqInhibit = irqInhibit;

    if (m_irqInhibit)
        clearInterrupt();

    if (m_mode == FiveStep)
    {
        for (FrameClock* unit : m_units)
        {
            unit->quarterFrame();
            unit->halfFrame();
        }
    }
}

void FrameSequencer::clearInterrupt()
{
    if (m_frameInterrupt)
    {
        m_frameInterrupt = false;
        m_irq.release();
    }
}
} // namespace apu

APU::APU(IAudioSink& sink, InterruptLine& irq, std::function<std::uint8_t(std::uint16_t)> dmcDma)
  : m_pulse1(true)
  , m_pulse2(false)
  , m_dmc(irq, std::move(dmcDma))
  , m_frameSequencer({ &m_pulse1.envelope,
                       &m_pulse1.sweep,
                       &m_pulse1.length,
                       &m_pulse2.envelope,
                       &m_pulse2.sweep,
                       &m_pulse2.length,
                       &m_triangle.length,
                       &m_triangle.linear,
                       &m_noise.envelope,
                       &m_noise.length },
                     irq)
  , m_sink(sink)
{
}

void APU::step()
{
    // The triangle, noise and DMC units run at the CPU clock.
    m_noise.tick();
    m_dmc.tick();
    m_triangle.tick();

    if (m_secondHalf)
    {
        // Everything else, including the DAC output, runs at half that rate.
        m_frameSequencer.clock();
        m_pulse1.tick();
        m_pulse2.tick();

        m_samples.push_back(mix(m_pulse1.sample(), m_pulse2.sample(), m_triangle.sample(), m_noise.sample(),
                                m_dmc.sample()));
    }

    m_secondHalf = !m_secondHalf;
}

void APU::writeRegister(std::uint16_t address, std::uint8_t value)
{
    switch (address)
    {
    case RegPulse1Control:
        m_pulse1.writeControl(value);
        break;
    case RegPulse1Sweep:
        m_pulse1.writeSweep(value);
        break;
    case RegPulse1Low:
        m_pulse1.writePeriodLow(value);
        break;
    case RegPulse1High:
        m_pulse1.writePeriodHigh(value);
        break;

    case RegPulse2Control:
        m_pulse2.writeControl(value);
        break;
    case RegPulse2Sweep:
        m_pulse2.writeSweep(value);
        break;
    case RegPulse2Low:
        m_pulse2.writePeriodLow(value);
        break;
    case RegPulse2High:
        m_pulse2.writePeriodHigh(value);
        break;

    case RegTriangleLinear:
        m_triangle.writeLinearControl(value);
        break;
    case RegTriangleLow:
        m_triangle.writePeriodLow(value);
        break;
    case RegTriangleHigh:
        m_triangle.writePeriodHigh(value);
        break;

    case RegNoiseControl:
        m_noise.writeControl(value);
        break;
    case RegNoisePeriod:
        m_noise.writePeriod(value);
        break;
    case RegNoiseLength:
        m_noise.writeLength(value);
        break;

    case RegDmcFrequency:
        m_dmc.writeFrequency(value);
        break;
    case RegDmcOutput:
        m_dmc.writeOutput(value);
        break;
    case RegDmcAddress:
        m_dmc.writeSampleAddress(value);
        break;
    case RegDmcLength:
        m_dmc.writeSampleLength(value);
        break;

    case RegStatus:
        m_pulse1.length.setEnabled((value & 0x01) != 0);
        m_pulse2.length.setEnabled((value & 0x02) != 0);
        m_triangle.length.setEnabled((value & 0x04) != 0);
        m_noise.length.setEnabled((value & 0x08) != 0);
        m_dmc.setEnabled((value & 0x10) != 0);
        break;

    case RegFrameControl:
        m_frameSequencer.reset((value >> 7) != 0 ? apu::FrameSequencer::FiveStep
                                                 : apu::FrameSequencer::FourStep,
                               (value >> 6) != 0);
        break;

    default:
        break;
    }
}

std::uint8_t APU::readStatus()
{
    const bool frameInterrupt = m_frameSequencer.interruptFlag();
    m_frameSequencer.clearInterrupt();

    const bool dmcInterrupt = m_dmc.interruptFlag();
    m_dmc.clearInterrupt();

    std::uint8_t status = 0;

    if (!m_pulse1.length.muted())
        status |= 0x01;
    if (!m_pulse2.length.muted())
        status |= 0x02;
    if (!m_triangle.length.muted())
        status |= 0x04;
    if (!m_noise.length.muted())
        status |= 0x08;
    if (m_dmc.hasSamples())
        status |= 0x10;
    if (frameInterrupt)
        status |= 0x40;
    if (dmcInterrupt)
        status |= 0x80;

    return status;
}

int APU::outputSampleRate()
{
    using namespace std::chrono;

    // The APU produces one sample every two CPU clock periods of 559 ns.
    const auto period = duration_cast<duration<double>>(nanoseconds(2 * 559));
    return static_cast<int>(1.0 / period.count());
}

void APU::flushAudio()
{
    if (m_samples.empty())
        return;

    if (!m_muted)
        m_sink.pushSamples(m_samples.data(), m_samples.size(), outputSampleRate());

    m_samples.clear();
}

void APU::reset()
{
    m_samples.clear();
    m_secondHalf = false;
}

float APU::mix(std::uint8_t pulse1, std::uint8_t pulse2, std::uint8_t triangle, std::uint8_t noise,
               std::uint8_t dmc)
{
    const float pulse1out = static_cast<float>(pulse1);
    const float pulse2out = static_cast<float>(pulse2);

    float pulseOut = 0;
    if (pulse1out + pulse2out != 0)
        pulseOut = static_cast<float>(95.88 / ((8128.0 / (pulse1out + pulse2out)) + 100.0));

    const float triangleout = static_cast<float>(triangle);
    const float noiseout    = static_cast<float>(noise);
    const float dmcout      = static_cast<float>(dmc);

    float tndOut = 0;
    if (triangleout + noiseout + dmcout != 0)
    {
        const float tndSum = (triangleout / 8227.0) + (noiseout / 12241.0) + (dmcout / 22638.0);
        tndOut             = static_cast<float>(159.79 / (1.0 / tndSum + 100.0));
    }

    return pulseOut + tndOut;
}

} // namespace allnes
