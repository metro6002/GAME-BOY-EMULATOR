#include "gb.h"

enum { Z = 0x80, N = 0x40, H = 0x20, C = 0x10 };

static uint16_t hl(const GbCpu *cpu)
{
    return (uint16_t)((cpu->h << 8) | cpu->l);
}

static void set_hl(GbCpu *cpu, uint16_t value)
{
    cpu->h = (uint8_t)(value >> 8);
    cpu->l = (uint8_t)value;
}

static uint16_t pair(const GbCpu *cpu, unsigned index)
{
    switch (index) {
    case 0: return (uint16_t)((cpu->b << 8) | cpu->c);
    case 1: return (uint16_t)((cpu->d << 8) | cpu->e);
    case 2: return hl(cpu);
    default: return cpu->sp;
    }
}

static void set_pair(GbCpu *cpu, unsigned index, uint16_t value)
{
    switch (index) {
    case 0: cpu->b = (uint8_t)(value >> 8); cpu->c = (uint8_t)value; break;
    case 1: cpu->d = (uint8_t)(value >> 8); cpu->e = (uint8_t)value; break;
    case 2: set_hl(cpu, value); break;
    default: cpu->sp = value; break;
    }
}

static uint8_t reg_read(GameBoy *gb, unsigned index)
{
    GbCpu *cpu = &gb->cpu;
    switch (index) {
    case 0: return cpu->b;
    case 1: return cpu->c;
    case 2: return cpu->d;
    case 3: return cpu->e;
    case 4: return cpu->h;
    case 5: return cpu->l;
    case 6: return gb_read(gb, hl(cpu));
    default: return cpu->a;
    }
}

static void reg_write(GameBoy *gb, unsigned index, uint8_t value)
{
    GbCpu *cpu = &gb->cpu;
    switch (index) {
    case 0: cpu->b = value; break;
    case 1: cpu->c = value; break;
    case 2: cpu->d = value; break;
    case 3: cpu->e = value; break;
    case 4: cpu->h = value; break;
    case 5: cpu->l = value; break;
    case 6: gb_write(gb, hl(cpu), value); break;
    default: cpu->a = value; break;
    }
}

static uint8_t fetch8(GameBoy *gb)
{
    uint8_t value = gb_read(gb, gb->cpu.pc);
    if (gb->cpu.halt_bug) gb->cpu.halt_bug = false;
    else ++gb->cpu.pc;
    return value;
}

static uint16_t fetch16(GameBoy *gb)
{
    uint8_t low = fetch8(gb);
    return (uint16_t)(low | (fetch8(gb) << 8));
}

static void push(GameBoy *gb, uint16_t value)
{
    gb_write(gb, --gb->cpu.sp, (uint8_t)(value >> 8));
    gb_write(gb, --gb->cpu.sp, (uint8_t)value);
}

static uint16_t pop(GameBoy *gb)
{
    uint8_t low = gb_read(gb, gb->cpu.sp++);
    return (uint16_t)(low | (gb_read(gb, gb->cpu.sp++) << 8));
}

static bool condition(const GbCpu *cpu, unsigned index)
{
    switch (index) {
    case 0: return !(cpu->f & Z);
    case 1: return (cpu->f & Z) != 0;
    case 2: return !(cpu->f & C);
    default: return (cpu->f & C) != 0;
    }
}

static void alu(GbCpu *cpu, unsigned operation, uint8_t value)
{
    unsigned carry = (cpu->f & C) ? 1 : 0;
    unsigned result;
    switch (operation) {
    case 0: case 1: /* ADD, ADC */
        if (operation == 0) carry = 0;
        result = (unsigned)cpu->a + value + carry;
        cpu->f = (uint8_t)(((result & 0xFF) == 0 ? Z : 0) |
                            (((cpu->a & 15) + (value & 15) + carry > 15) ? H : 0) |
                            (result > 0xFF ? C : 0));
        cpu->a = (uint8_t)result;
        break;
    case 2: case 3: case 7: /* SUB, SBC, CP */
        if (operation != 3) carry = 0;
        result = (unsigned)cpu->a - value - carry;
        cpu->f = (uint8_t)(((result & 0xFF) == 0 ? Z : 0) | N |
                            ((cpu->a & 15) < (unsigned)(value & 15) + carry ? H : 0) |
                            ((unsigned)cpu->a < (unsigned)value + carry ? C : 0));
        if (operation != 7) cpu->a = (uint8_t)result;
        break;
    case 4: cpu->a &= value; cpu->f = (cpu->a ? 0 : Z) | H; break;
    case 5: cpu->a ^= value; cpu->f = cpu->a ? 0 : Z; break;
    case 6: cpu->a |= value; cpu->f = cpu->a ? 0 : Z; break;
    }
}

static uint8_t increment(GbCpu *cpu, uint8_t value)
{
    uint8_t result = (uint8_t)(value + 1);
    cpu->f = (uint8_t)((cpu->f & C) | (result == 0 ? Z : 0) |
                       ((value & 15) == 15 ? H : 0));
    return result;
}

static uint8_t decrement(GbCpu *cpu, uint8_t value)
{
    uint8_t result = (uint8_t)(value - 1);
    cpu->f = (uint8_t)((cpu->f & C) | N | (result == 0 ? Z : 0) |
                       ((value & 15) == 0 ? H : 0));
    return result;
}

static void add_hl(GbCpu *cpu, uint16_t value)
{
    uint16_t old = hl(cpu);
    unsigned sum = (unsigned)old + value;
    cpu->f = (uint8_t)((cpu->f & Z) |
                       (((old & 0xFFF) + (value & 0xFFF) > 0xFFF) ? H : 0) |
                       (sum > 0xFFFF ? C : 0));
    set_hl(cpu, (uint16_t)sum);
}

static uint16_t add_sp_signed(GbCpu *cpu, uint8_t byte)
{
    uint16_t sp = cpu->sp;
    cpu->f = (uint8_t)((((sp & 15) + (byte & 15) > 15) ? H : 0) |
                       (((sp & 0xFF) + byte > 0xFF) ? C : 0));
    return (uint16_t)(sp + (int8_t)byte);
}

static void daa(GbCpu *cpu)
{
    unsigned correction = 0;
    bool carry = (cpu->f & C) != 0;
    if (!(cpu->f & N)) {
        if ((cpu->f & H) || (cpu->a & 15) > 9) correction |= 0x06;
        if (carry || cpu->a > 0x99) { correction |= 0x60; carry = true; }
        cpu->a = (uint8_t)(cpu->a + correction);
    } else {
        if (cpu->f & H) correction |= 0x06;
        if (carry) correction |= 0x60;
        cpu->a = (uint8_t)(cpu->a - correction);
    }
    cpu->f = (uint8_t)((cpu->f & N) | (cpu->a == 0 ? Z : 0) |
                       (carry ? C : 0));
}

static unsigned cb_instruction(GameBoy *gb)
{
    GbCpu *cpu = &gb->cpu;
    uint8_t opcode = fetch8(gb);
    unsigned target = opcode & 7;
    unsigned group = opcode >> 6;
    unsigned bit = (opcode >> 3) & 7;
    uint8_t value = reg_read(gb, target);
    if (group == 1) {
        cpu->f = (uint8_t)((cpu->f & C) | H |
                           ((value & (1u << bit)) ? 0 : Z));
        return target == 6 ? 12 : 8;
    }
    if (group == 2) value &= (uint8_t)~(1u << bit);
    else if (group == 3) value |= (uint8_t)(1u << bit);
    else {
        unsigned carry_in = (cpu->f & C) ? 1 : 0;
        unsigned carry_out = 0;
        switch (bit) {
        case 0: carry_out = value >> 7; value = (uint8_t)((value << 1) | carry_out); break;
        case 1: carry_out = value & 1; value = (uint8_t)((value >> 1) | (carry_out << 7)); break;
        case 2: carry_out = value >> 7; value = (uint8_t)((value << 1) | carry_in); break;
        case 3: carry_out = value & 1; value = (uint8_t)((value >> 1) | (carry_in << 7)); break;
        case 4: carry_out = value >> 7; value <<= 1; break;
        case 5: carry_out = value & 1; value = (uint8_t)((value >> 1) | (value & 0x80)); break;
        case 6: value = (uint8_t)((value << 4) | (value >> 4)); break;
        case 7: carry_out = value & 1; value >>= 1; break;
        }
        cpu->f = (uint8_t)((value == 0 ? Z : 0) | (carry_out ? C : 0));
    }
    reg_write(gb, target, value);
    return target == 6 ? 16 : 8;
}

static GbStepResult finish(GameBoy *gb, unsigned cycles)
{
    gb_tick(gb, cycles);
    if (gb->cpu.ime_delay && --gb->cpu.ime_delay == 0) gb->cpu.ime = true;
    return GB_STEP_OK;
}

GbStepResult gb_step(GameBoy *gb, uint8_t *opcode)
{
    GbCpu *cpu = &gb->cpu;
    uint8_t pending = (uint8_t)(gb->ie & gb->io[0x0F] & 0x1F);
    *opcode = 0;
    if (cpu->stopped) { gb_tick(gb, 4); return GB_STEP_STOPPED; }
    if (cpu->halted) {
        if (!pending) { gb_tick(gb, 4); return GB_STEP_OK; }
        cpu->halted = false;
    }
    if (cpu->ime && pending) {
        unsigned bit = 0;
        while (!(pending & (1u << bit))) ++bit;
        cpu->ime = false;
        gb->io[0x0F] &= (uint8_t)~(1u << bit);
        push(gb, cpu->pc);
        cpu->pc = (uint16_t)(0x40 + 8 * bit);
        gb_tick(gb, 20);
        return GB_STEP_OK;
    }

    uint8_t op = fetch8(gb);
    *opcode = op;
    unsigned cycles = 4;
    uint8_t byte;
    uint16_t word;

    if (op >= 0x40 && op <= 0x7F) {
        if (op == 0x76) {
            if (!cpu->ime && pending) cpu->halt_bug = true;
            else cpu->halted = true;
        } else {
            reg_write(gb, (op >> 3) & 7, reg_read(gb, op & 7));
            cycles = ((op & 7) == 6 || ((op >> 3) & 7) == 6) ? 8 : 4;
        }
        return finish(gb, cycles);
    }
    if (op >= 0x80 && op <= 0xBF) {
        alu(cpu, (op >> 3) & 7, reg_read(gb, op & 7));
        return finish(gb, (op & 7) == 6 ? 8 : 4);
    }
    if ((op & 0xC7) == 0x04) {
        unsigned r = (op >> 3) & 7;
        reg_write(gb, r, increment(cpu, reg_read(gb, r)));
        return finish(gb, r == 6 ? 12 : 4);
    }
    if ((op & 0xC7) == 0x05) {
        unsigned r = (op >> 3) & 7;
        reg_write(gb, r, decrement(cpu, reg_read(gb, r)));
        return finish(gb, r == 6 ? 12 : 4);
    }
    if ((op & 0xC7) == 0x06) {
        unsigned r = (op >> 3) & 7;
        reg_write(gb, r, fetch8(gb));
        return finish(gb, r == 6 ? 12 : 8);
    }
    if ((op & 0xCF) == 0x01) {
        set_pair(cpu, (op >> 4) & 3, fetch16(gb));
        return finish(gb, 12);
    }
    if ((op & 0xCF) == 0x03) {
        unsigned r = (op >> 4) & 3;
        set_pair(cpu, r, (uint16_t)(pair(cpu, r) + 1));
        return finish(gb, 8);
    }
    if ((op & 0xCF) == 0x0B) {
        unsigned r = (op >> 4) & 3;
        set_pair(cpu, r, (uint16_t)(pair(cpu, r) - 1));
        return finish(gb, 8);
    }
    if ((op & 0xCF) == 0x09) {
        add_hl(cpu, pair(cpu, (op >> 4) & 3));
        return finish(gb, 8);
    }
    if ((op & 0xE7) == 0x20) {
        byte = fetch8(gb);
        if (condition(cpu, (op >> 3) & 3)) {
            cpu->pc = (uint16_t)(cpu->pc + (int8_t)byte);
            cycles = 12;
        } else cycles = 8;
        return finish(gb, cycles);
    }
    if ((op & 0xE7) == 0xC0) {
        if (condition(cpu, (op >> 3) & 3)) {
            cpu->pc = pop(gb);
            cycles = 20;
        } else cycles = 8;
        return finish(gb, cycles);
    }
    if ((op & 0xE7) == 0xC2) {
        word = fetch16(gb);
        if (condition(cpu, (op >> 3) & 3)) { cpu->pc = word; cycles = 16; }
        else cycles = 12;
        return finish(gb, cycles);
    }
    if ((op & 0xE7) == 0xC4) {
        word = fetch16(gb);
        if (condition(cpu, (op >> 3) & 3)) {
            push(gb, cpu->pc);
            cpu->pc = word;
            cycles = 24;
        } else cycles = 12;
        return finish(gb, cycles);
    }
    if ((op & 0xCF) == 0xC1) {
        unsigned r = (op >> 4) & 3;
        word = pop(gb);
        if (r == 3) { cpu->a = (uint8_t)(word >> 8); cpu->f = (uint8_t)(word & 0xF0); }
        else set_pair(cpu, r, word);
        return finish(gb, 12);
    }
    if ((op & 0xCF) == 0xC5) {
        unsigned r = (op >> 4) & 3;
        word = r == 3 ? (uint16_t)((cpu->a << 8) | cpu->f) : pair(cpu, r);
        push(gb, word);
        return finish(gb, 16);
    }
    if ((op & 0xC7) == 0xC6) {
        alu(cpu, (op >> 3) & 7, fetch8(gb));
        return finish(gb, 8);
    }
    if ((op & 0xC7) == 0xC7) {
        push(gb, cpu->pc);
        cpu->pc = (uint16_t)(op & 0x38);
        return finish(gb, 16);
    }

    switch (op) {
    case 0x00: break; /* NOP */
    case 0x02: gb_write(gb, pair(cpu, 0), cpu->a); cycles = 8; break;
    case 0x12: gb_write(gb, pair(cpu, 1), cpu->a); cycles = 8; break;
    case 0x0A: cpu->a = gb_read(gb, pair(cpu, 0)); cycles = 8; break;
    case 0x1A: cpu->a = gb_read(gb, pair(cpu, 1)); cycles = 8; break;
    case 0x07: case 0x0F: case 0x17: case 0x1F: {
        unsigned old_carry = (cpu->f & C) ? 1 : 0;
        unsigned new_carry;
        if (op == 0x07 || op == 0x17) {
            new_carry = cpu->a >> 7;
            cpu->a = (uint8_t)((cpu->a << 1) |
                               (op == 0x07 ? new_carry : old_carry));
        } else {
            new_carry = cpu->a & 1;
            cpu->a = (uint8_t)((cpu->a >> 1) |
                               ((op == 0x0F ? new_carry : old_carry) << 7));
        }
        cpu->f = new_carry ? C : 0;
        break;
    }
    case 0x08:
        word = fetch16(gb);
        gb_write(gb, word, (uint8_t)cpu->sp);
        gb_write(gb, (uint16_t)(word + 1), (uint8_t)(cpu->sp >> 8));
        cycles = 20;
        break;
    case 0x10: fetch8(gb); cpu->stopped = true; break;
    case 0x18:
        byte = fetch8(gb);
        cpu->pc = (uint16_t)(cpu->pc + (int8_t)byte);
        cycles = 12;
        break;
    case 0x22: case 0x32:
        word = hl(cpu); gb_write(gb, word, cpu->a);
        set_hl(cpu, (uint16_t)(word + (op == 0x22 ? 1 : -1)));
        cycles = 8;
        break;
    case 0x2A: case 0x3A:
        word = hl(cpu); cpu->a = gb_read(gb, word);
        set_hl(cpu, (uint16_t)(word + (op == 0x2A ? 1 : -1)));
        cycles = 8;
        break;
    case 0x27: daa(cpu); break;
    case 0x2F: cpu->a = (uint8_t)~cpu->a; cpu->f |= N | H; break;
    case 0x37: cpu->f = (uint8_t)((cpu->f & Z) | C); break;
    case 0x3F: cpu->f = (uint8_t)((cpu->f & Z) | ((cpu->f & C) ? 0 : C)); break;
    case 0xC3: cpu->pc = fetch16(gb); cycles = 16; break;
    case 0xC9: cpu->pc = pop(gb); cycles = 16; break;
    case 0xCB: cycles = cb_instruction(gb); break;
    case 0xCD: word = fetch16(gb); push(gb, cpu->pc); cpu->pc = word; cycles = 24; break;
    case 0xD9: cpu->pc = pop(gb); cpu->ime = true; cpu->ime_delay = 0; cycles = 16; break;
    case 0xE0: gb_write(gb, (uint16_t)(0xFF00 | fetch8(gb)), cpu->a); cycles = 12; break;
    case 0xE2: gb_write(gb, (uint16_t)(0xFF00 | cpu->c), cpu->a); cycles = 8; break;
    case 0xE8: byte = fetch8(gb); cpu->sp = add_sp_signed(cpu, byte); cycles = 16; break;
    case 0xE9: cpu->pc = hl(cpu); break;
    case 0xEA: gb_write(gb, fetch16(gb), cpu->a); cycles = 16; break;
    case 0xF0: cpu->a = gb_read(gb, (uint16_t)(0xFF00 | fetch8(gb))); cycles = 12; break;
    case 0xF2: cpu->a = gb_read(gb, (uint16_t)(0xFF00 | cpu->c)); cycles = 8; break;
    case 0xF3: cpu->ime = false; cpu->ime_delay = 0; break;
    case 0xF8: byte = fetch8(gb); set_hl(cpu, add_sp_signed(cpu, byte)); cycles = 12; break;
    case 0xF9: cpu->sp = hl(cpu); cycles = 8; break;
    case 0xFA: cpu->a = gb_read(gb, fetch16(gb)); cycles = 16; break;
    case 0xFB: cpu->ime_delay = 2; break;
    default: return GB_STEP_ILLEGAL;
    }
    return finish(gb, cycles);
}
