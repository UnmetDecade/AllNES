#!/usr/bin/env python3
"""Generate a tiny NROM test ROM for AllNES.

The ROM draws 8-pixel vertical stripes (black/white) using the background,
which makes it easy to check the PPU pipeline, nametable/pattern fetches and
the ARGB frame buffer without shipping a copyrighted game.

Usage: make_test_rom.py [output.nes]
"""

import sys

# NES palette indexes used by the test pattern.
BACKDROP = 0x0F  # black
WHITE = 0x30

PALETTE = [
    BACKDROP, WHITE, 0x16, 0x27,
    BACKDROP, WHITE, 0x16, 0x27,
    BACKDROP, WHITE, 0x16, 0x27,
    BACKDROP, WHITE, 0x16, 0x27,
    BACKDROP, WHITE, 0x16, 0x27,
    BACKDROP, WHITE, 0x16, 0x27,
    BACKDROP, WHITE, 0x16, 0x27,
    BACKDROP, WHITE, 0x16, 0x27,
]


class Asm:
    """A five-minute 6502 assembler: only the addressing modes we need."""

    def __init__(self, org=0xC000):
        self.org = org
        self.code = bytearray()
        self.labels = {}
        self.fixups = []  # (kind, operand_pos, label)

    def label(self, name):
        self.labels[name] = self.org + len(self.code)

    def op(self, *values):
        self.code.extend(values)

    def imm(self, opcode, value):
        self.op(opcode, value & 0xFF)

    def abs(self, opcode, addr):
        """Absolute addressing with an int or a label name."""
        self.op(opcode)
        pos = len(self.code)
        if isinstance(addr, str):
            self.op(0, 0)
            self.fixups.append(("abs", pos, addr))
        else:
            self.op(addr & 0xFF, (addr >> 8) & 0xFF)

    def abs_x(self, opcode, addr):
        self.op(opcode)
        pos = len(self.code)
        if isinstance(addr, str):
            self.op(0, 0)
            self.fixups.append(("abs", pos, addr))
        else:
            self.op(addr & 0xFF, (addr >> 8) & 0xFF)

    def branch(self, opcode, label):
        self.op(opcode)
        pos = len(self.code)
        self.op(0)
        self.fixups.append(("rel", pos, label))

    def assemble(self):
        for kind, pos, label in self.fixups:
            if label not in self.labels:
                raise ValueError("undefined label: %s" % label)
            target = self.labels[label]
            if kind == "rel":
                offset = target - (self.org + pos + 1)
                if not -128 <= offset <= 127:
                    raise ValueError("branch to %s out of range" % label)
                self.code[pos] = offset & 0xFF
            else:
                self.code[pos] = target & 0xFF
                self.code[pos + 1] = (target >> 8) & 0xFF
        return bytes(self.code)


def build_program():
    a = Asm(0xC000)

    a.op(0x78)              # SEI
    a.op(0xD8)              # CLD
    a.imm(0xA2, 0xFF)       # LDX #$FF
    a.op(0x9A)              # TXS

    a.label("wait_vblank")
    a.abs(0x2C, 0x2002)     # BIT $2002
    a.branch(0x10, "wait_vblank")  # BPL wait_vblank

    a.imm(0xA9, 0x00)       # LDA #0
    a.abs(0x8D, 0x2000)     # STA $2000 (NMI off)
    a.abs(0x8D, 0x2001)     # STA $2001 (rendering off)

    # PPUADDR = $3F00
    a.imm(0xA9, 0x3F)
    a.abs(0x8D, 0x2006)
    a.imm(0xA9, 0x00)
    a.abs(0x8D, 0x2006)

    a.imm(0xA2, 0x00)       # LDX #0
    a.label("pal_loop")
    a.abs_x(0xBD, "palette")  # LDA palette,X
    a.abs(0x8D, 0x2007)       # STA $2007
    a.op(0xE8)                # INX
    a.imm(0xE0, 32)           # CPX #32
    a.branch(0xD0, "pal_loop")  # BNE

    # PPUADDR = $2000
    a.imm(0xA9, 0x20)
    a.abs(0x8D, 0x2006)
    a.imm(0xA9, 0x00)
    a.abs(0x8D, 0x2006)

    # 32x30 tiles, alternating 0/1 horizontally -> 8px vertical stripes.
    a.imm(0xA0, 0x00)       # LDY #0
    a.label("row_loop")
    a.imm(0xA2, 0x00)       # LDX #0
    a.label("col_loop")
    a.op(0x8A)              # TXA
    a.imm(0x29, 0x01)       # AND #1
    a.abs(0x8D, 0x2007)     # STA $2007
    a.op(0xE8)              # INX
    a.imm(0xE0, 32)         # CPX #32
    a.branch(0xD0, "col_loop")
    a.op(0xC8)              # INY
    a.imm(0xC0, 30)         # CPY #30
    a.branch(0xD0, "row_loop")

    # Attribute table: 64 bytes, palette 0 everywhere.
    a.imm(0xA2, 0x00)       # LDX #0
    a.label("attr_loop")
    a.imm(0xA9, 0x00)       # LDA #0
    a.abs(0x8D, 0x2007)     # STA $2007
    a.op(0xE8)              # INX
    a.imm(0xE0, 64)         # CPX #64
    a.branch(0xD0, "attr_loop")

    # Scroll = 0,0; enable background rendering from pattern table 0.
    a.imm(0xA9, 0x00)
    a.abs(0x8D, 0x2005)
    a.abs(0x8D, 0x2005)
    a.imm(0xA9, 0x08)       # background pattern table at $0000
    a.abs(0x8D, 0x2000)
    a.imm(0xA9, 0x0A)       # show background, no left-edge clipping
    a.abs(0x8D, 0x2001)

    a.label("halt")
    a.abs(0x4C, "halt")     # JMP halt

    a.label("palette")
    a.op(*PALETTE)

    return a.assemble()


def build_rom():
    prg = bytearray(0x4000)
    code = build_program()
    prg[0:len(code)] = code

    # Vectors (PRG offset = address - $C000).
    prg[0x3FFA:0x3FFC] = (0xC000).to_bytes(2, "little")  # NMI
    prg[0x3FFC:0x3FFE] = (0xC000).to_bytes(2, "little")  # RESET
    prg[0x3FFE:0x4000] = (0xC000).to_bytes(2, "little")  # IRQ/BRK

    chr_rom = bytearray(0x2000)
    # Tile 0: all zero -> colour 0 (backdrop).
    # Tile 1: bitplane 0 all ones, bitplane 1 zero -> colour 1 (white).
    chr_rom[16:24] = b"\xFF" * 8

    header = bytearray(b"NES\x1A")
    header += bytes([1, 1, 0x00, 0x00])  # 16KB PRG, 8KB CHR, mapper 0, horizontal
    header += bytes(8)

    return bytes(header + prg + chr_rom)


def main():
    output = sys.argv[1] if len(sys.argv) > 1 else "test_stripes.nes"
    data = build_rom()
    with open(output, "wb") as f:
        f.write(data)
    print("wrote %s (%d bytes)" % (output, len(data)))


if __name__ == "__main__":
    main()
