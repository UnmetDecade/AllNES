#pragma once

namespace allnes
{
// Interrupt vectors at the top of the address space.
constexpr int NmiVector   = 0xFFFA;
constexpr int ResetVector = 0xFFFC;
constexpr int IrqVector   = 0xFFFE;

// Base number of CPU cycles for every opcode. Zero marks an opcode this
// interpreter does not execute; the table is indexed directly by the opcode
// byte, so the layout mirrors the 6502 opcode map.
inline constexpr int CycleCost[0x100] = {
    // clang-format off
    7, 6, 0, 0, 0, 3, 5, 0, 3, 2, 2, 0, 0, 4, 6, 0,
    2, 5, 0, 0, 0, 4, 6, 0, 2, 4, 0, 0, 0, 4, 7, 0,
    6, 6, 0, 0, 3, 3, 5, 0, 4, 2, 2, 0, 4, 4, 6, 0,
    2, 5, 0, 0, 0, 4, 6, 0, 2, 4, 0, 0, 0, 4, 7, 0,
    6, 6, 0, 0, 0, 3, 5, 0, 3, 2, 2, 0, 3, 4, 6, 0,
    2, 5, 0, 0, 0, 4, 6, 0, 2, 4, 0, 0, 0, 4, 7, 0,
    6, 6, 0, 0, 0, 3, 5, 0, 4, 2, 2, 0, 5, 4, 6, 0,
    2, 5, 0, 0, 0, 4, 6, 0, 2, 4, 0, 0, 0, 4, 7, 0,
    0, 6, 0, 0, 3, 3, 3, 0, 2, 0, 2, 0, 4, 4, 4, 0,
    2, 6, 0, 0, 4, 4, 4, 0, 2, 5, 2, 0, 0, 5, 0, 0,
    2, 6, 2, 0, 3, 3, 3, 0, 2, 2, 2, 0, 4, 4, 4, 0,
    2, 5, 0, 0, 4, 4, 4, 0, 2, 4, 2, 0, 4, 4, 4, 0,
    2, 6, 0, 0, 3, 3, 5, 0, 2, 2, 2, 0, 4, 4, 6, 0,
    2, 5, 0, 0, 0, 4, 6, 0, 2, 4, 0, 0, 0, 4, 7, 0,
    2, 6, 0, 0, 3, 3, 5, 0, 2, 2, 2, 2, 4, 4, 6, 0,
    2, 5, 0, 0, 0, 4, 6, 0, 2, 4, 0, 0, 0, 4, 7, 0,
    // clang-format on
};

// Opcodes whose low two bits are 0b01 form the general ALU/load/store group.
// Bits 7-5 choose the operation and bits 4-2 the addressing mode.
enum AluOperation
{
    AluOra = 0,
    AluAnd,
    AluEor,
    AluAdc,
    AluSta,
    AluLda,
    AluCmp,
    AluSbc,
};

enum AluAddressing
{
    AluIndexedIndirect, // ($nn,X)
    AluZeroPage,        // $nn
    AluImmediate,       // #$nn
    AluAbsolute,        // $nnnn
    AluIndirectIndexed, // ($nn),Y
    AluZeroPageIndexed, // $nn,X
    AluAbsoluteIndexedY,
    AluAbsoluteIndexedX,
};

// Opcodes with low bits 0b10 are read/modify/write or indexed loads/stores.
enum RmwOperation
{
    RmwAsl = 0,
    RmwRol,
    RmwLsr,
    RmwRor,
    RmwStx,
    RmwLdx,
    RmwDec,
    RmwInc,
};

// Opcodes with low bits 0b00 cover the remaining bit tests and compares.
enum MiscOperation
{
    MiscBit = 1,
    MiscSty = 4,
    MiscLdy,
    MiscCpy,
    MiscCpx,
};

enum OperandMode
{
    OperandImmediate = 0,
    OperandZeroPage,
    OperandAccumulator,
    OperandAbsolute,
    OperandZeroPageIndexed = 5,
    OperandAbsoluteIndexed = 7,
};

enum InterruptKind
{
    InterruptIrq,
    InterruptNmi,
    InterruptBrk,
};

// Branch opcodes: 0bxx010000. Bit 5 selects the polarity and bits 7-6 pick
// the flag to test.
enum BranchFlag
{
    FlagNegative = 0,
    FlagOverflow,
    FlagCarry,
    FlagZero,
};
} // namespace allnes
