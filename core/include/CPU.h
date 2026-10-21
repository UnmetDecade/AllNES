#pragma once

#include "CPUOpcodes.h"
#include "IRQ.h"
#include "MainBus.h"

#include <memory>
#include <vector>

namespace allnes
{
class CPU;

// One interrupt source. Every source owns a bit in the CPU's request latch;
// cartridges use it for scanline and expansion-audio interrupts.
class IRQHandler : public InterruptLine
{
public:
    IRQHandler(CPU& cpu, int bit);

    void pull() override;
    void release() override;

private:
    CPU& m_cpu;
    int  m_bit;
};

// Ricoh 2A03 core: the 6502 instruction set without decimal arithmetic, with
// the CPU's two-cycle stall for OAM DMA exposed to the bus owner.
class CPU
{
public:
    explicit CPU(MainBus& mem);

    void step();
    void reset();
    void reset(Address startAddress);

    Address getPC() const { return m_pc; }

    void skipOAMDMACycles();
    void skipDMCDMACycles();

    void nmiInterrupt();

    InterruptLine& createIRQHandler();
    void       setIRQPulldown(int bit, bool state);

private:
    // Instruction groups, tried in this order: implied, branch, ALU, RMW,
    // miscellaneous. Each returns true when it consumed the opcode.
    bool executeImplied(Byte opcode);
    bool executeBranch(Byte opcode);
    bool executeAlu(Byte opcode);
    bool executeRmw(Byte opcode);
    bool executeMisc(Byte opcode);

    void    enterInterrupt(InterruptKind kind);
    void    pullStatus(Byte flags);
    Address readWord(Address address);

    void push(Byte value);
    Byte pull();

    void setZeroNegative(int value);
    void chargePageCross(Address base, Address indexed);

    bool interruptPending() const { return !m_irqDisabled && m_irqLine != 0; }
    Byte statusByte(bool withBreak) const;

    int m_stallCycles = 0;
    int m_cycleCount  = 0;

    Address m_pc = 0;
    Byte    m_sp = 0;
    Byte    m_a  = 0;
    Byte    m_x  = 0;
    Byte    m_y  = 0;

    bool m_carry     = false;
    bool m_zero      = false;
    bool m_irqDisabled = false;
    bool m_decimal   = false;
    bool m_overflow  = false;
    bool m_negative  = false;

    bool m_nmiPending = false;
    // Cycle at which the NMI line was asserted, and the first cycle of the
    // instruction currently in flight. The 6502 never abandons an
    // instruction in progress: an NMI that arrives while one is executing is
    // serviced after it completes, which matters for games that poll $2002
    // and race the NMI handler for the vblank flag.
    int m_nmiRequestCycle      = 0;
    int m_instructionStartCycle = 0;

    MainBus& m_bus;

    // One bit per registered interrupt source.
    unsigned int                                m_irqLine = 0;
    std::vector<std::unique_ptr<IRQHandler>>    m_irqSources;
};
} // namespace allnes
