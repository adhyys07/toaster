import os
import sys

OPS = {
    ('LDA', 'imm'): (0xA9, 1), ('LDA', 'abs'): (0xAD, 2), ('LDA', 'absx'): (0xBD, 2),
    ('LDA', 'zp'):  (0xA5, 1), ('LDA', 'absy'): (0xB9, 2), ('LDA', 'izy'): (0xB1, 1),
    ('LDX', 'imm'): (0xA2, 1), ('LDX', 'abs'): (0xAE, 2), ('LDX', 'zp'): (0xA6, 1),
    ('LDY', 'imm'): (0xA0, 1), ('LDY', 'abs'): (0xAC, 2), ('LDY', 'zp'): (0xA4, 1),
    ('STA', 'abs'): (0x8D, 2), ('STA', 'zp'): (0x85, 1), ('STA', 'absx'): (0x9D, 2),
    ('STA', 'izy'): (0x91, 1),
    ('STX', 'abs'): (0x8E, 2), ('STX', 'zp'): (0x86, 1),
    ('STY', 'abs'): (0x8C, 2), ('STY', 'zp'): (0x84, 1),
    ('ADC', 'imm'): (0x69, 1), ('SBC', 'imm'): (0xE9, 1),
    ('AND', 'imm'): (0x29, 1), ('ORA', 'imm'): (0x09, 1), ('EOR', 'imm'): (0x49, 1),
    ('CMP', 'imm'): (0xC9, 1), ('CPX', 'imm'): (0xE0, 1), ('CPY', 'imm'): (0xC0, 1),
    ('INC', 'zp'):  (0xE6, 1), ('DEC', 'zp'): (0xC6, 1),
    ('INC', 'abs'): (0xEE, 2), ('DEC', 'abs'): (0xCE, 2),
    ('JMP', 'abs'): (0x4C, 2), ('JMP', 'ind'): (0x6C, 2), ('JSR', 'abs'): (0x20, 2),
    ('ASL', 'acc'): (0x0A, 0), ('LSR', 'acc'): (0x4A, 0),
    ('ROL', 'acc'): (0x2A, 0), ('ROR', 'acc'): (0x6A, 0),
}

IMPLIED = {
    'INX': 0xE8, 'INY': 0xC8, 'DEX': 0xCA, 'DEY': 0x88,
    'TAX': 0xAA, 'TAY': 0xA8, 'TXA': 0x8A, 'TYA': 0x98,
    'TSX': 0xBA, 'TXS': 0x9A, 'PHA': 0x48, 'PLA': 0x68,
    'PHP': 0x08, 'PLP': 0x28, 'RTS': 0x60, 'RTI': 0x40,
    'CLC': 0x18, 'SEC': 0x38, 'CLI': 0x58, 'SEI': 0x78,
    'CLV': 0xB8, 'CLD': 0xD8, 'SED': 0xF8, 'NOP': 0xEA, 'BRK': 0x00,
}

BRANCHES = {
    'BCC': 0x90, 'BCS': 0xB0, 'BEQ': 0xF0, 'BNE': 0xD0,
    'BMI': 0x30, 'BPL': 0x10, 'BVC': 0x50, 'BVS': 0x70,
}


def assemble(source, org=0x8000):
    lines = []
    for raw in source.strip().split('\n'):
        line = raw.split(';')[0].strip()
        if line:
            lines.append(line)

    # --- pass 1: resolve labels to addresses --------------------------------
    labels, pc = {}, org
    for line in lines:
        if line.endswith(':'):
            labels[line[:-1]] = pc
            continue
        pc += instruction_size(line)

    # --- pass 2: emit -------------------------------------------------------
    out, pc = bytearray(), org
    for line in lines:
        if line.endswith(':'):
            continue
        out += encode(line, pc, labels)
        pc = org + len(out)
    return bytes(out)


def split_operand(line):
    parts = line.split(None, 1)
    return parts[0].upper(), (parts[1].strip() if len(parts) > 1 else '')


def classify(mnemonic, operand):
    if mnemonic in IMPLIED:
        return 'imp', None
    if mnemonic in BRANCHES:
        return 'rel', operand
    if operand in ('A', 'a', ''):
        return 'acc', None
    if operand.startswith('#'):
        return 'imm', operand[1:]
    if operand.startswith('('):
        inner = operand.strip('()')
        if inner.endswith(',Y') or inner.endswith(',y'):
            return 'izy', inner[:-2]
        return 'ind', inner
    if operand.upper().endswith(',X'):
        value = operand[:-2]
        return ('zp' + 'x' if is_zp(value) else 'absx'), value
    if operand.upper().endswith(',Y'):
        value = operand[:-2]
        return 'absy', value
    return ('zp' if is_zp(operand) else 'abs'), operand


def is_zp(text):
    try:
        return parse_number(text) < 0x100
    except Exception:
        return False


def parse_number(text):
    text = text.strip()
    if text.startswith('$'):
        return int(text[1:], 16)
    if text.startswith('%'):
        return int(text[1:], 2)
    return int(text)


def resolve(text, labels):
    text = text.strip()
    if text in labels:
        return labels[text]
    return parse_number(text)


def instruction_size(line):
    mnemonic, operand = split_operand(line)
    if mnemonic in IMPLIED:
        return 1
    if mnemonic in BRANCHES:
        return 2
    mode, _ = classify(mnemonic, operand)
    if mode == 'acc':
        return 1
    key = (mnemonic, mode)
    if key not in OPS:
        raise SystemExit('unsupported: %s (%s)' % (line, mode))
    return 1 + OPS[key][1]


def encode(line, pc, labels):
    mnemonic, operand = split_operand(line)

    if mnemonic in IMPLIED:
        return bytes([IMPLIED[mnemonic]])

    if mnemonic in BRANCHES:
        target = resolve(operand, labels)
        offset = target - (pc + 2)
        if not -128 <= offset <= 127:
            raise SystemExit('branch out of range: %s' % line)
        return bytes([BRANCHES[mnemonic], offset & 0xFF])

    mode, value = classify(mnemonic, operand)
    if mode == 'acc':
        return bytes([OPS[(mnemonic, 'acc')][0]])

    opcode, length = OPS[(mnemonic, mode)]
    n = resolve(value, labels)
    if length == 1:
        return bytes([opcode, n & 0xFF])
    return bytes([opcode, n & 0xFF, (n >> 8) & 0xFF])


def build_rom(code, chr_data=None, org=0x8000):
    header = bytearray(b'NES\x1a')
    header += bytes([1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])

    prg = bytearray(16384)
    prg[org - 0x8000:org - 0x8000 + len(code)] = code
    prg[0x3FFA] = org & 0xFF          # NMI
    prg[0x3FFB] = (org >> 8) & 0xFF
    prg[0x3FFC] = org & 0xFF          # RESET
    prg[0x3FFD] = (org >> 8) & 0xFF
    prg[0x3FFE] = org & 0xFF          # IRQ
    prg[0x3FFF] = (org >> 8) & 0xFF

    chr_rom = bytearray(8192)
    if chr_data:
        chr_rom[:len(chr_data)] = chr_data

    return bytes(header + prg + chr_rom)



def background_rom():
    code = assemble("""
        SEI
        CLD
        LDA #$00
        STA $2000
        STA $2001

        LDA #$3F          ; PPUADDR = $3F00, the palette
        STA $2006
        LDA #$00
        STA $2006
        LDA #$0F          ; universal background: black
        STA $2007
        LDA #$30          ; palette 0 colour 1: white
        STA $2007

        LDA #$20          ; PPUADDR = $2000, nametable 0
        STA $2006
        LDA #$00
        STA $2006
        LDX #$04          ; four pages of $100 bytes
        LDY #$00
    fill:
        LDA #$01          ; tile index 1
        STA $2007
        DEY
        BNE fill
        DEX
        BNE fill

        ; That fill ran 1024 bytes and so spilled past the 960-byte nametable
        ; into the attribute table. Put it back to palette 0 everywhere.
        LDA #$23
        STA $2006
        LDA #$C0
        STA $2006
        LDX #$40          ; 64 attribute bytes
    attrs:
        LDA #$00
        STA $2007
        DEX
        BNE attrs

        ; v and t are the same register, so the $2006 writes above left fine Y
        ; set to 2. Rewriting $2005 clears the scroll before rendering starts.
        LDA #$00
        STA $2005
        STA $2005

        LDA #$0A          ; PPUMASK: show background, including the left 8px
        STA $2001
    spin:
        JMP spin
    """)

    chr_data = bytearray(16 * 2)
    for i in range(8):
        chr_data[16 + i] = 0xFF       # tile 1, bitplane 0
        chr_data[16 + 8 + i] = 0x00   # tile 1, bitplane 1
    return build_rom(code, chr_data)


def sprite_rom():
    code = assemble("""
        SEI
        CLD
        LDA #$00
        STA $2000
        STA $2001

        LDA #$3F          ; palette
        STA $2006
        LDA #$00
        STA $2006
        LDA #$0F
        STA $2007
        LDA #$0F
        STA $2007
        LDA #$0F
        STA $2007
        LDA #$0F
        STA $2007

        LDA #$3F          ; sprite palette 0 at $3F11
        STA $2006
        LDA #$11
        STA $2006
        LDA #$16          ; red
        STA $2007

        LDA #$00          ; clear page 2 (the DMA source) to $FF = offscreen
        LDX #$00
        LDA #$FF
    clear:
        STA $0200,X
        INX
        BNE clear

        LDA #$20          ; sprite 0: Y = 32
        STA $0200
        LDA #$01          ; tile 1
        STA $0201
        LDA #$00          ; attributes: palette 0, in front
        STA $0202
        LDA #$30          ; X = 48
        STA $0203

        LDA #$02          ; OAM DMA from page $0200
        STA $4014

        LDA #$10          ; PPUMASK: show sprites
        STA $2001
    spin:
        JMP spin
    """)

    chr_data = bytearray(16 * 2)
    for i in range(8):
        chr_data[16 + i] = 0xFF
        chr_data[16 + 8 + i] = 0x00
    return build_rom(code, chr_data)


def arithmetic_rom():
    code = assemble("""
        SEI
        CLD
        CLC
        LDA #$50          ; $50 + $50 = $A0, signed overflow
        ADC #$50
        STA $00           ; expect $A0
        PHP
        PLA
        STA $01           ; status after the overflowing add

        SEC
        LDA #$50
        SBC #$20
        STA $02           ; expect $30

        LDA #$FF
        STA $10
        INC $10
        LDA $10
        STA $03           ; expect $00 after wrap

        LDA #$01
        ASL A
        ASL A
        ASL A
        STA $04           ; expect $08

        LDX #$05
        LDY #$0A
        TXA
        STA $05           ; expect $05
        TYA
        STA $06           ; expect $0A

        JSR sub
        STA $07           ; expect $99 set by the subroutine
    spin:
        JMP spin
    sub:
        LDA #$99
        RTS
    """)
    return build_rom(code)


def chip8_font_rom():
    # Draw the built-in zero glyph at the top-left, then keep the image on
    # screen. This small ROM also exercises address loads, sprite drawing,
    # and the jump instruction in the CHIP-8 core.
    return bytes([
        0xA2, 0x0A,       # I = address of the five-byte zero glyph below
        0x60, 0x00,       # V0 = x = 0
        0x61, 0x00,       # V1 = y = 0
        0xD0, 0x15,       # draw 5 rows at (V0, V1)
        0x12, 0x08,       # loop here
        0xF0, 0x90, 0x90, 0x90, 0xF0,
    ])


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else 'roms'
    os.makedirs(out_dir, exist_ok=True)

    for name, builder in (('bgtest', background_rom),
                          ('sprite', sprite_rom),
                          ('arith', arithmetic_rom)):
        path = os.path.join(out_dir, name + '.nes')
        data = builder()
        with open(path, 'wb') as handle:
            handle.write(data)
        print('wrote %s (%d bytes)' % (path, len(data)))

    path = os.path.join(out_dir, 'fonttest.ch8')
    data = chip8_font_rom()
    with open(path, 'wb') as handle:
        handle.write(data)
    print('wrote %s (%d bytes)' % (path, len(data)))


if __name__ == '__main__':
    main()
