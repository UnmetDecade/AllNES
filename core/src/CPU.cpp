#include "CPU.h"
#include "Log.h"

#include <cstdint>
#include <iomanip>

namespace allnes
{
namespace
{
// Stack lives on page one; the CPU keeps the stack pointer as an offset.
constexpr Address StackPage = 0x0100;

// Implied opcodes.
constexpr Byte OpBrk = 0x00;
constexpr Byte OpJsr = 0x20;
constexpr Byte OpRti = 0x40;
constexpr Byte OpRts = 0x60;
constexpr Byte OpJmp = 0x4C;
constexpr Byte OpJmpIndirect = 0x6C;
constexpr Byte OpPhp = 0x08;
constexpr Byte OpPlp = 0x28;
constexpr Byte OpPha = 0x48;
constexpr Byte OpPla = 0x68;
constexpr Byte OpDey = 0x88;
constexpr Byte OpDex = 0xCA;
constexpr Byte OpTay = 0xA8;
constexpr Byte OpIny = 0xC8;
constexpr Byte OpInx = 0xE8;
constexpr Byte OpClc = 0x18;
constexpr Byte OpSec = 0x38;
constexpr Byte OpCli = 0x58;
constexpr Byte OpSei = 0x78;
constexpr Byte OpTya = 0x98;
constexpr Byte OpClv = 0xB8;
constexpr Byte OpCld = 0xD8;
constexpr Byte OpSed = 0xF8;
constexpr Byte OpTxa = 0x8A;
constexpr Byte OpTxs = 0x9A;
constexpr Byte OpTax = 0xAA;
constexpr Byte OpTsx = 0xBA;
constexpr Byte OpNop = 0xEA;
} // namespace

IRQHandler::IRQHandler(CPU& cpu, int bit)
  : m_cpu(cpu)
  , m_bit(bit)
{
}

void IRQHandler::pull()
{
    m_cpu.setIRQPulldown(m_bit, true);
}

void IRQHandler::release()
{
    m_cpu.setIRQPulldown(m_bit, false);
}

CPU::CPU(MainBus& mem)
  : m_bus(mem)
{
}

void CPU::reset()
{
    reset(readWord(ResetVector));
}

void CPU::reset(Address startAddress)
{
    m_stallCycles = 0;
    m_cycleCount  = 0;

    m_a = 0;
    m_x = 0;
    m_y = 0;

    m_irqDisabled = true;
    m_carry       = false;
    m_decimal     = false;
    m_negative    = false;
    m_overflow    = false;
    m_zero        = false;

    m_pc = startAddress;
    m_sp = 0xFD;

    m_nmiPending           = false;
    m_nmiRequestCycle      = 0;
    m_instructionStartCycle = 0;
}

void CPU::nmiInterrupt()
{
    m_nmiPending      = true;
    m_nmiRequestCycle = m_cycleCount;
}

InterruptLine& CPU::createIRQHandler()
{
    // Each source owns one bit of the shared request latch.
    const int bit = static_cast<int>(m_irqSources.size());
    m_irqSources.push_back(std::make_unique<IRQHandler>(*this, bit));
    return *m_irqSources.back();
}

void CPU::setIRQPulldown(int bit, bool state)
{
    // A released source clears its assigned bit; an asserted source raises
    // the shared request line.
    const unsigned int mask = 1u << bit;
    m_irqLine               = (m_irqLine & ~mask) | (state ? mask : 0u);
}

void CPU::skipOAMDMACycles()
{
    // 512 transfer cycles plus one alignment cycle on odd CPU cycles.
    m_stallCycles += 513;
    m_stallCycles += (m_cycleCount & 1);
}

void CPU::skipDMCDMACycles()
{
    // The exact cost depends on the fetch alignment; the average is enough
    // for sample generation.
    m_stallCycles += 3;
}

Byte CPU::statusByte(bool withBreak) const
{
    return static_cast<Byte>((m_negative ? 0x80 : 0x00) | (m_overflow ? 0x40 : 0x00) | 0x20 |
                             (withBreak ? 0x10 : 0x00) | (m_decimal ? 0x08 : 0x00) |
                             (m_irqDisabled ? 0x04 : 0x00) | (m_zero ? 0x02 : 0x00) |
                             (m_carry ? 0x01 : 0x00));
}

void CPU::step()
{
    ++m_cycleCount;

    if (m_stallCycles-- > 1)
        return;

    m_stallCycles = 0;

    // NMI cannot be masked and is serviced before IRQ. If the edge arrived
    // while the instruction that is ready to run was already in flight, the
    // instruction still completes first (the 6502 never abandons one), and
    // the NMI is taken right after it.
    bool deferNmi = false;
    if (m_nmiPending)
    {
        if (m_nmiRequestCycle >= m_instructionStartCycle)
        {
            deferNmi = true;
        }
        else
        {
            m_nmiPending = false;
            enterInterrupt(InterruptNmi);
            return;
        }
    }

    if (!deferNmi && interruptPending())
    {
        enterInterrupt(InterruptIrq);
        return;
    }

    LOG_CPU << std::hex << std::setfill('0') << std::uppercase << std::setw(4) << +m_pc << "  " << std::setw(2)
            << +m_bus.read(m_pc) << "  "
            << "A:" << std::setw(2) << +m_a << " "
            << "X:" << std::setw(2) << +m_x << " "
            << "Y:" << std::setw(2) << +m_y << " "
            << "P:" << std::setw(2) << +statusByte(false) << " "
            << "SP:" << std::setw(2) << +m_sp << " "
            << "CYC:" << std::setw(3) << std::setfill(' ') << std::dec << ((m_cycleCount - 1) * 3) % 341
            << std::endl;

    const Byte opcode = m_bus.read(m_pc++);
    const int  cost   = CycleCost[opcode];

    // The groups are mutually exclusive, but trying them in order keeps the
    // implied and branch opcodes (which do not follow the low-bit pattern)
    // ahead of the patterned groups.
    if (cost != 0 && (executeImplied(opcode) || executeBranch(opcode) || executeAlu(opcode) ||
                      executeRmw(opcode) || executeMisc(opcode)))
    {
        m_stallCycles += cost;
    }
    else
    {
        LOG(Error) << "Unrecognized opcode: " << std::hex << +opcode << " at $" << (m_pc - 1)
                   << std::dec << std::endl;
    }

    // The next instruction's first cycle is the next CPU cycle.
    m_instructionStartCycle = m_cycleCount + 1;

    if (deferNmi)
    {
        m_nmiPending = false;
        enterInterrupt(InterruptNmi);
    }
}

bool CPU::executeImplied(Byte opcode)
{
    switch (opcode)
    {
    case OpNop:
        break;
    case OpBrk:
        enterInterrupt(InterruptBrk);
        break;
    case OpJsr:
        // The pushed return address points at the last byte of the JSR
        // operand so that RTS can simply add one.
        push(static_cast<Byte>((m_pc + 1) >> 8));
        push(static_cast<Byte>(m_pc + 1));
        m_pc = readWord(m_pc);
        break;
    case OpRts:
        m_pc = pull();
        m_pc |= static_cast<Address>(pull() << 8);
        ++m_pc;
        break;
    case OpRti:
        pullStatus(pull());
        m_pc = pull();
        m_pc |= static_cast<Address>(pull() << 8);
        break;
    case OpJmp:
        m_pc = readWord(m_pc);
        break;
    case OpJmpIndirect:
    {
        // The high byte is fetched from the same page as the low byte.
        const Address pointer = readWord(m_pc);
        const Address page    = pointer & 0xFF00;
        m_pc = m_bus.read(pointer) |
               static_cast<Address>(m_bus.read(page | static_cast<Address>((pointer + 1) & 0x00FF)) << 8);
    }
    break;
    case OpPhp:
        // PHP always reports the break flag as set.
        push(statusByte(true));
        break;
    case OpPlp:
        pullStatus(pull());
        break;
    case OpPha:
        push(m_a);
        break;
    case OpPla:
        m_a = pull();
        setZeroNegative(m_a);
        break;
    case OpDey:
        setZeroNegative(--m_y);
        break;
    case OpDex:
        setZeroNegative(--m_x);
        break;
    case OpTay:
        m_y = m_a;
        setZeroNegative(m_y);
        break;
    case OpIny:
        setZeroNegative(++m_y);
        break;
    case OpInx:
        setZeroNegative(++m_x);
        break;
    case OpClc:
        m_carry = false;
        break;
    case OpSec:
        m_carry = true;
        break;
    case OpCli:
        m_irqDisabled = false;
        break;
    case OpSei:
        m_irqDisabled = true;
        break;
    case OpCld:
        m_decimal = false;
        break;
    case OpSed:
        m_decimal = true;
        break;
    case OpTya:
        m_a = m_y;
        setZeroNegative(m_a);
        break;
    case OpClv:
        m_overflow = false;
        break;
    case OpTxa:
        m_a = m_x;
        setZeroNegative(m_a);
        break;
    case OpTxs:
        m_sp = m_x;
        break;
    case OpTax:
        m_x = m_a;
        setZeroNegative(m_x);
        break;
    case OpTsx:
        m_x = m_sp;
        setZeroNegative(m_x);
        break;
    default:
        return false;
    }

    return true;
}

bool CPU::executeBranch(Byte opcode)
{
    if ((opcode & 0x1F) != 0x10)
        return false;

    // Bit 5 picks whether the branch expects the flag set or clear; the top
    // two bits select the flag.
    bool expectation = (opcode & 0x20) != 0;
    bool flag        = false;

    switch ((opcode >> 6) & 0x03)
    {
    case FlagNegative:
        flag = m_negative;
        break;
    case FlagOverflow:
        flag = m_overflow;
        break;
    case FlagCarry:
        flag = m_carry;
        break;
    case FlagZero:
        flag = m_zero;
        break;
    default:
        return false;
    }

    if (!(expectation ^ flag))
    {
        const int8_t offset = static_cast<int8_t>(m_bus.read(m_pc++));
        ++m_stallCycles;

        const Address target = static_cast<Address>(m_pc + offset);
        chargePageCross(m_pc, target);
        m_pc = target;
    }
    else
    {
        ++m_pc;
    }

    return true;
}

bool CPU::executeAlu(Byte opcode)
{
    if ((opcode & 0x03) != 0x01)
        return false;

    const AluOperation operation = static_cast<AluOperation>((opcode & 0xE0) >> 5);
    Address            location  = 0;

    switch (static_cast<AluAddressing>((opcode & 0x1C) >> 2))
    {
    case AluIndexedIndirect:
    {
        const Byte pointer = static_cast<Byte>(m_bus.read(m_pc++) + m_x);
        location = m_bus.read(pointer) | static_cast<Address>(m_bus.read(static_cast<Byte>(pointer + 1)) << 8);
    }
    break;
    case AluZeroPage:
        location = m_bus.read(m_pc++);
        break;
    case AluImmediate:
        location = m_pc++;
        break;
    case AluAbsolute:
        location = readWord(m_pc);
        m_pc += 2;
        break;
    case AluIndirectIndexed:
    {
        const Byte pointer = m_bus.read(m_pc++);
        location = m_bus.read(pointer) | static_cast<Address>(m_bus.read(static_cast<Byte>(pointer + 1)) << 8);
        if (operation != AluSta)
            chargePageCross(location, static_cast<Address>(location + m_y));
        location = static_cast<Address>(location + m_y);
    }
    break;
    case AluZeroPageIndexed:
        location = static_cast<Byte>(m_bus.read(m_pc++) + m_x);
        break;
    case AluAbsoluteIndexedY:
        location = readWord(m_pc);
        m_pc += 2;
        if (operation != AluSta)
            chargePageCross(location, static_cast<Address>(location + m_y));
        location = static_cast<Address>(location + m_y);
        break;
    case AluAbsoluteIndexedX:
        location = readWord(m_pc);
        m_pc += 2;
        if (operation != AluSta)
            chargePageCross(location, static_cast<Address>(location + m_x));
        location = static_cast<Address>(location + m_x);
        break;
    default:
        return false;
    }

    switch (operation)
    {
    case AluOra:
        m_a |= m_bus.read(location);
        setZeroNegative(m_a);
        break;
    case AluAnd:
        m_a &= m_bus.read(location);
        setZeroNegative(m_a);
        break;
    case AluEor:
        m_a ^= m_bus.read(location);
        setZeroNegative(m_a);
        break;
    case AluAdc:
    {
        const Byte          operand = m_bus.read(location);
        const std::uint16_t sum =
          static_cast<std::uint16_t>(m_a + operand + (m_carry ? 1 : 0));

        m_carry    = (sum & 0x0100) != 0;
        m_overflow = ((m_a ^ sum) & (operand ^ sum) & 0x80) != 0;
        m_a        = static_cast<Byte>(sum);
        setZeroNegative(m_a);
    }
    break;
    case AluSta:
        m_bus.write(location, m_a);
        break;
    case AluLda:
        m_a = m_bus.read(location);
        setZeroNegative(m_a);
        break;
    case AluCmp:
    {
        const std::uint16_t difference = static_cast<std::uint16_t>(m_a - m_bus.read(location));
        m_carry                        = (difference & 0x0100) == 0;
        setZeroNegative(difference);
    }
    break;
    case AluSbc:
    {
        const std::uint16_t subtrahend = m_bus.read(location);
        const std::uint16_t difference =
          static_cast<std::uint16_t>(m_a - subtrahend - (m_carry ? 0 : 1));

        m_carry    = (difference & 0x0100) == 0;
        m_overflow = ((m_a ^ difference) & (~subtrahend ^ difference) & 0x80) != 0;
        m_a        = static_cast<Byte>(difference);
        setZeroNegative(difference);
    }
    break;
    default:
        return false;
    }

    return true;
}

bool CPU::executeRmw(Byte opcode)
{
    if ((opcode & 0x03) != 0x02)
        return false;

    const RmwOperation operation = static_cast<RmwOperation>((opcode & 0xE0) >> 5);
    const OperandMode  mode      = static_cast<OperandMode>((opcode & 0x1C) >> 2);
    Address            location  = 0;

    switch (mode)
    {
    case OperandImmediate:
        location = m_pc++;
        break;
    case OperandZeroPage:
        location = m_bus.read(m_pc++);
        break;
    case OperandAccumulator:
        break;
    case OperandAbsolute:
        location = readWord(m_pc);
        m_pc += 2;
        break;
    case OperandZeroPageIndexed:
    {
        // LDX and STX use Y for their zero-page indexed form.
        const Byte index =
          (operation == RmwLdx || operation == RmwStx) ? m_y : m_x;
        location = static_cast<Byte>(m_bus.read(m_pc++) + index);
    }
    break;
    case OperandAbsoluteIndexed:
    {
        const Byte index =
          (operation == RmwLdx || operation == RmwStx) ? m_y : m_x;
        location = readWord(m_pc);
        m_pc += 2;
        chargePageCross(location, static_cast<Address>(location + index));
        location = static_cast<Address>(location + index);
    }
    break;
    default:
        return false;
    }

    switch (operation)
    {
    case RmwAsl:
    case RmwRol:
        if (mode == OperandAccumulator)
        {
            const bool previousCarry = m_carry;
            m_carry                  = (m_a & 0x80) != 0;
            m_a                      = static_cast<Byte>(m_a << 1);
            if (operation == RmwRol && previousCarry)
                m_a |= 0x01;
            setZeroNegative(m_a);
        }
        else
        {
            const bool          previousCarry = m_carry;
            const Byte          value         = m_bus.read(location);
            std::uint16_t       shifted       = static_cast<std::uint16_t>(value << 1);

            m_carry = (value & 0x80) != 0;
            if (operation == RmwRol && previousCarry)
                shifted |= 0x01;

            setZeroNegative(shifted);
            m_bus.write(location, static_cast<Byte>(shifted));
        }
        break;
    case RmwLsr:
    case RmwRor:
        if (mode == OperandAccumulator)
        {
            const bool previousCarry = m_carry;
            m_carry                  = (m_a & 0x01) != 0;
            m_a                      = static_cast<Byte>(m_a >> 1);
            if (operation == RmwRor && previousCarry)
                m_a |= 0x80;
            setZeroNegative(m_a);
        }
        else
        {
            const bool    previousCarry = m_carry;
            const Byte    value         = m_bus.read(location);
            std::uint16_t shifted       = static_cast<std::uint16_t>(value >> 1);

            m_carry = (value & 0x01) != 0;
            if (operation == RmwRor && previousCarry)
                shifted |= 0x80;

            setZeroNegative(shifted);
            m_bus.write(location, static_cast<Byte>(shifted));
        }
        break;
    case RmwStx:
        m_bus.write(location, m_x);
        break;
    case RmwLdx:
        m_x = m_bus.read(location);
        setZeroNegative(m_x);
        break;
    case RmwDec:
    {
        const int value = m_bus.read(location) - 1;
        setZeroNegative(value);
        m_bus.write(location, static_cast<Byte>(value));
    }
    break;
    case RmwInc:
    {
        const int value = m_bus.read(location) + 1;
        setZeroNegative(value);
        m_bus.write(location, static_cast<Byte>(value));
    }
    break;
    default:
        return false;
    }

    return true;
}

bool CPU::executeMisc(Byte opcode)
{
    if ((opcode & 0x03) != 0x00)
        return false;

    Address location = 0;

    switch (static_cast<OperandMode>((opcode & 0x1C) >> 2))
    {
    case OperandImmediate:
        location = m_pc++;
        break;
    case OperandZeroPage:
        location = m_bus.read(m_pc++);
        break;
    case OperandAbsolute:
        location = readWord(m_pc);
        m_pc += 2;
        break;
    case OperandZeroPageIndexed:
        location = static_cast<Byte>(m_bus.read(m_pc++) + m_x);
        break;
    case OperandAbsoluteIndexed:
        location = readWord(m_pc);
        m_pc += 2;
        chargePageCross(location, static_cast<Address>(location + m_x));
        location = static_cast<Address>(location + m_x);
        break;
    default:
        return false;
    }

    switch (static_cast<MiscOperation>((opcode & 0xE0) >> 5))
    {
    case MiscBit:
    {
        const Byte operand = m_bus.read(location);
        m_zero             = (m_a & operand) == 0;
        m_overflow         = (operand & 0x40) != 0;
        m_negative         = (operand & 0x80) != 0;
    }
    break;
    case MiscSty:
        m_bus.write(location, m_y);
        break;
    case MiscLdy:
        m_y = m_bus.read(location);
        setZeroNegative(m_y);
        break;
    case MiscCpy:
    {
        const std::uint16_t difference = static_cast<std::uint16_t>(m_y - m_bus.read(location));
        m_carry                        = (difference & 0x0100) == 0;
        setZeroNegative(difference);
    }
    break;
    case MiscCpx:
    {
        const std::uint16_t difference = static_cast<std::uint16_t>(m_x - m_bus.read(location));
        m_carry                        = (difference & 0x0100) == 0;
        setZeroNegative(difference);
    }
    break;
    default:
        return false;
    }

    return true;
}

void CPU::enterInterrupt(InterruptKind kind)
{
    if (m_irqDisabled && kind != InterruptNmi && kind != InterruptBrk)
        return;

    if (kind == InterruptBrk)
        ++m_pc;

    push(static_cast<Byte>(m_pc >> 8));
    push(static_cast<Byte>(m_pc));
    push(statusByte(kind == InterruptBrk));

    m_irqDisabled = true;

    m_pc = readWord(kind == InterruptNmi ? NmiVector : IrqVector);
    m_stallCycles += 7;
}

void CPU::pullStatus(Byte flags)
{
    m_negative    = (flags & 0x80) != 0;
    m_overflow    = (flags & 0x40) != 0;
    m_decimal     = (flags & 0x08) != 0;
    m_irqDisabled = (flags & 0x04) != 0;
    m_zero        = (flags & 0x02) != 0;
    m_carry       = (flags & 0x01) != 0;
}

Address CPU::readWord(Address address)
{
    return m_bus.read(address) |
           static_cast<Address>(m_bus.read(static_cast<Address>(address + 1)) << 8);
}

void CPU::push(Byte value)
{
    m_bus.write(StackPage | m_sp, value);
    --m_sp;
}

Byte CPU::pull()
{
    return m_bus.read(StackPage | ++m_sp);
}

void CPU::setZeroNegative(int value)
{
    const Byte byte = static_cast<Byte>(value);
    m_zero          = byte == 0;
    m_negative      = (byte & 0x80) != 0;
}

void CPU::chargePageCross(Address base, Address indexed)
{
    if ((base & 0xFF00) != (indexed & 0xFF00))
        ++m_stallCycles;
}

} // namespace allnes
