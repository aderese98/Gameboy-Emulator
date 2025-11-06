#include "cpu.h"
#include "bus.h"
#include <cstdio>

Cpu::Cpu(Bus& bus) : bus_(bus) {}

void Cpu::reset_no_bios() {
    af_ = 0x01B0;
    bc_ = 0x0013;
    de_ = 0x00D8;
    hl_ = 0x014D;
    sp_ = 0xFFFE;
    pc_ = 0x0100;
    setF(static_cast<u8>(F() & 0xF0));
}

std::string Cpu::dump() const {
    char buf[200];
    std::snprintf(buf, sizeof(buf),
                  "AF=%04X  BC=%04X  DE=%04X  HL=%04X  SP=%04X  PC=%04X  (A=%02X F=%02X B=%02X C=%02X D=%02X E=%02X H=%02X L=%02X)",
                  AF(), BC(), DE(), HL(), SP(), PC(),
                  A(), F(), B(), C(), D(), E(), H(), L());
    return std::string(buf);
}

// ----------------- small helpers -----------------
u8 Cpu::fetch8() {
    u8 v = bus_.read8(pc_);
    pc_ = static_cast<u16>(pc_ + 1);
    return v;
}

u16 Cpu::fetch16() {
    u8 lo = fetch8();
    u8 hi = fetch8();
    return static_cast<u16>((hi << 8) | lo);
}

void Cpu::set_flag(Flag f, bool on) {
    u8 fval = F();
    if (on) fval |= f; else fval &= static_cast<u8>(~f);
    setF(fval);
}

bool Cpu::get_flag(Flag f) const {
    return (F() & f) != 0;
}

u8 Cpu::alu_inc8(u8 v) {
    u8 res = static_cast<u8>(v + 1);
    set_flag(FLAG_Z, res == 0);
    set_flag(FLAG_N, false);
    // half-carry if lower nibble overflowed
    set_flag(FLAG_H, (v & 0x0F) == 0x0F);
    // C unchanged
    return res;
}

u8 Cpu::alu_dec8(u8 v) {
    u8 res = static_cast<u8>(v - 1);
    set_flag(FLAG_Z, res == 0);
    set_flag(FLAG_N, true);
    // half-borrow if we borrowed from bit 4
    set_flag(FLAG_H, (v & 0x0F) == 0x00);
    // C unchanged
    return res;
}

u8 Cpu::get_reg_by_index(int idx) const {
    switch (idx) {
        case 0: return B();
        case 1: return C();
        case 2: return D();
        case 3: return E();
        case 4: return H();
        case 5: return L();
        case 6: /* (HL) not handled here */ return 0;
        case 7: return A();
        default: return 0;
    }
}

void Cpu::set_reg_by_index(int idx, u8 v) {
    switch (idx) {
        case 0: setB(v); break;
        case 1: setC(v); break;
        case 2: setD(v); break;
        case 3: setE(v); break;
        case 4: setH(v); break;
        case 5: setL(v); break;
        case 6: /* (HL) would write to memory */ break;
        case 7: setA(v); break;
        default: break;
    }
}

// ----------------- fetch/decode/execute -----------------
int Cpu::step() {
    const u16 pc_before = pc_;
    const u8 op = fetch8();

    if (trace_) {
        auto line = disasm(pc_before, op);
        std::printf("%04X: %-16s  %s\n", pc_before, line.c_str(), dump().c_str());
    }

    switch (op) {
        // ---- NOP ----
        case 0x00: // NOP
            return 4;

        // ---- LD r, d8 (B,C,D,E,H,L,A) ----
        case 0x06: { setB(fetch8()); return 8; } // LD B,d8
        case 0x0E: { setC(fetch8()); return 8; } // LD C,d8
        case 0x16: { setD(fetch8()); return 8; } // LD D,d8
        case 0x1E: { setE(fetch8()); return 8; } // LD E,d8
        case 0x26: { setH(fetch8()); return 8; } // LD H,d8
        case 0x2E: { setL(fetch8()); return 8; } // LD L,d8
        case 0x3E: { setA(fetch8()); return 8; } // LD A,d8

        // ---- INC r (B,C,D,E,H,L,A) ----
        case 0x04: { setB(alu_inc8(B())); return 4; } // INC B
        case 0x0C: { setC(alu_inc8(C())); return 4; } // INC C
        case 0x14: { setD(alu_inc8(D())); return 4; } // INC D
        case 0x1C: { setE(alu_inc8(E())); return 4; } // INC E
        case 0x24: { setH(alu_inc8(H())); return 4; } // INC H
        case 0x2C: { setL(alu_inc8(L())); return 4; } // INC L
        case 0x3C: { setA(alu_inc8(A())); return 4; } // INC A

        // ---- DEC r (B,C,D,E,H,L,A) ----
        case 0x05: { setB(alu_dec8(B())); return 4; } // DEC B
        case 0x0D: { setC(alu_dec8(C())); return 4; } // DEC C
        case 0x15: { setD(alu_dec8(D())); return 4; } // DEC D
        case 0x1D: { setE(alu_dec8(E())); return 4; } // DEC E
        case 0x25: { setH(alu_dec8(H())); return 4; } // DEC H
        case 0x2D: { setL(alu_dec8(L())); return 4; } // DEC L
        case 0x3D: { setA(alu_dec8(A())); return 4; } // DEC A

        // ---- LD (HL), d8 ----
        case 0x36: {
            u8 val = fetch8();
            bus_.write8(HL(), val);
            return 12;
        }

        // ---- 16-bit loads: LD rr, d16 ----
        case 0x01: { setBC(fetch16()); return 12; } // LD BC,d16
        case 0x11: { setDE(fetch16()); return 12; } // LD DE,d16
        case 0x21: { setHL(fetch16()); return 12; } // LD HL,d16
        case 0x31: { setSP(fetch16()); return 12; } // LD SP,d16

        // ---- HL auto inc/dec block moves ----
        case 0x22: { // LD (HL+),A
            bus_.write8(HL(), A());
            setHL(static_cast<u16>(HL() + 1));
            return 8;
        }
        case 0x2A: { // LD A,(HL+)
            setA(bus_.read8(HL()));
            setHL(static_cast<u16>(HL() + 1));
            return 8;
        }
        case 0x32: { // LD (HL-),A
            bus_.write8(HL(), A());
            setHL(static_cast<u16>(HL() - 1));
            return 8;
        }
        case 0x3A: { // LD A,(HL-)
            setA(bus_.read8(HL()));
            setHL(static_cast<u16>(HL() - 1));
            return 8;
        }

        // ---- ADD HL, rr ----
        case 0x09: { // ADD HL,BC
            u32 sum = static_cast<u32>(HL()) + static_cast<u32>(BC());
            set_flag(FLAG_N, false);
            set_flag(FLAG_H, ((HL() & 0x0FFF) + (BC() & 0x0FFF)) > 0x0FFF);
            set_flag(FLAG_C, sum > 0xFFFF);
            setHL(static_cast<u16>(sum));
            return 8;
        }
        case 0x19: { // ADD HL,DE
            u32 sum = static_cast<u32>(HL()) + static_cast<u32>(DE());
            set_flag(FLAG_N, false);
            set_flag(FLAG_H, ((HL() & 0x0FFF) + (DE() & 0x0FFF)) > 0x0FFF);
            set_flag(FLAG_C, sum > 0xFFFF);
            setHL(static_cast<u16>(sum));
            return 8;
        }
        case 0x29: { // ADD HL,HL
            u32 sum = static_cast<u32>(HL()) + static_cast<u32>(HL());
            set_flag(FLAG_N, false);
            set_flag(FLAG_H, ((HL() & 0x0FFF) + (HL() & 0x0FFF)) > 0x0FFF);
            set_flag(FLAG_C, sum > 0xFFFF);
            setHL(static_cast<u16>(sum));
            return 8;
        }
        case 0x39: { // ADD HL,SP
            u32 sum = static_cast<u32>(HL()) + static_cast<u32>(SP());
            set_flag(FLAG_N, false);
            set_flag(FLAG_H, ((HL() & 0x0FFF) + (SP() & 0x0FFF)) > 0x0FFF);
            set_flag(FLAG_C, sum > 0xFFFF);
            setHL(static_cast<u16>(sum));
            return 8;
        }

        // ---- Stack: PUSH rr ----
        case 0xC5: { push16(BC()); return 16; } // PUSH BC
        case 0xD5: { push16(DE()); return 16; } // PUSH DE
        case 0xE5: { push16(HL()); return 16; } // PUSH HL
        case 0xF5: { push16(AF()); return 16; } // PUSH AF

        // ---- Stack: POP rr ----
        case 0xC1: { setBC(pop16()); return 12; } // POP BC
        case 0xD1: { setDE(pop16()); return 12; } // POP DE
        case 0xE1: { setHL(pop16()); return 12; } // POP HL
        case 0xF1: { // POP AF (low nibble of F must be 0)
            u16 v = pop16();
            u8 a = static_cast<u8>(v >> 8);
            u8 f = static_cast<u8>(v & 0xF0);
            setA(a);
            setF(f);
            return 12;
        }

        // ---------- Relative jumps (JR) ----------
        case 0x18: { // JR r8
            i8 off = as_i8(fetch8());
            pc_ = static_cast<u16>(pc_ + off);
            return 12;
        }
        case 0x20: { // JR NZ, r8
            i8 off = as_i8(fetch8());
            if (condNZ()) { pc_ = static_cast<u16>(pc_ + off); return 12; }
            return 8;
        }
        case 0x28: { // JR Z, r8
            i8 off = as_i8(fetch8());
            if (condZ())  { pc_ = static_cast<u16>(pc_ + off); return 12; }
            return 8;
        }
        case 0x30: { // JR NC, r8
            i8 off = as_i8(fetch8());
            if (condNC()) { pc_ = static_cast<u16>(pc_ + off); return 12; }
            return 8;
        }
        case 0x38: { // JR C, r8
            i8 off = as_i8(fetch8());
            if (condC())  { pc_ = static_cast<u16>(pc_ + off); return 12; }
            return 8;
        }

        // ---------- Absolute jumps (JP) ----------
        case 0xC3: { // JP a16
            u16 a = fetch16();
            pc_ = a;
            return 16;
        }
        case 0xC2: { // JP NZ,a16
            u16 a = fetch16();
            if (condNZ()) { pc_ = a; return 16; }
            return 12;
        }
        case 0xCA: { // JP Z,a16
            u16 a = fetch16();
            if (condZ()) { pc_ = a; return 16; }
            return 12;
        }
        case 0xD2: { // JP NC,a16
            u16 a = fetch16();
            if (condNC()) { pc_ = a; return 16; }
            return 12;
        }
        case 0xDA: { // JP C,a16
            u16 a = fetch16();
            if (condC()) { pc_ = a; return 16; }
            return 12;
        }

        // ---------- Calls / Returns ----------
        case 0xCD: { // CALL a16
            u16 a = fetch16();
            push16(pc_);
            pc_ = a;
            return 24;
        }
        case 0xC4: { // CALL NZ,a16
            u16 a = fetch16();
            if (condNZ()) { push16(pc_); pc_ = a; return 24; }
            return 12;
        }
        case 0xCC: { // CALL Z,a16
            u16 a = fetch16();
            if (condZ()) { push16(pc_); pc_ = a; return 24; }
            return 12;
        }
        case 0xD4: { // CALL NC,a16
            u16 a = fetch16();
            if (condNC()) { push16(pc_); pc_ = a; return 24; }
            return 12;
        }
        case 0xDC: { // CALL C,a16
            u16 a = fetch16();
            if (condC()) { push16(pc_); pc_ = a; return 24; }
            return 12;
        }

        case 0xC9: { // RET
            pc_ = pop16();
            return 16;
        }
        case 0xC0: { // RET NZ
            if (condNZ()) { pc_ = pop16(); return 20; }
            return 8;
        }
        case 0xC8: { // RET Z
            if (condZ())  { pc_ = pop16(); return 20; }
            return 8;
        }
        case 0xD0: { // RET NC
            if (condNC()) { pc_ = pop16(); return 20; }
            return 8;
        }
        case 0xD8: { // RET C
            if (condC())  { pc_ = pop16(); return 20; }
            return 8;
        }

        // ---------- RST (call to fixed vectors) ----------
        case 0xC7: case 0xCF: case 0xD7: case 0xDF:
        case 0xE7: case 0xEF: case 0xF7: case 0xFF: {
            u16 vec = static_cast<u16>(op & 0x38); // vectors: 00,08,10,...,38
            push16(pc_);
            pc_ = vec;
            return 16;
        }
        

        // ---- LD r, r' block (0x40..0x7F) ----
        default:
            if(op >= 0x40 && op <= 0x7F) {
                if (op == 0x76) {
                    return 4;
                }
                int dst = (op >> 3) & 0x07;
                int src = (op >> 0) & 0x07;

                if (dst == 6 && src == 6) {
                    return 4;
                }
                // Read from source
                u8 value = 0;
                if (src == 6) {
                    value = bus_.read8(HL());  // LD r, (HL)
                } else {
                    value = get_reg_by_index(src); // LD r, r'
                }

                if (dst == 6) {
                    bus_.write8(HL(), value); // LD (HL, r
                    return 8;
                } else {
                    set_reg_by_index(dst, value); // LD r, r'
                    return (src == 6) ? 8 : 4;
                }
            }

            return 4;
    }
}

static const char* reg_name_by_index(int idx) {
    switch (idx) {
        case 0: return "B";
        case 1: return "C";
        case 2: return "D";
        case 3: return "E";
        case 4: return "H";
        case 5: return "L";
        case 6: return "(HL)";
        case 7: return "A";
        default: return "?";
    }
}

std::string Cpu::disasm(u16 pc_before, u8 op) const {
    char buf[64];

    switch (op) {
        case 0x00: return "NOP";
        case 0x01: return "LD BC, d16";
        case 0x06: std::snprintf(buf, sizeof(buf), "LD B, d8"); return buf;
        case 0x09: return "ADD HL, BC";
        case 0x0E: std::snprintf(buf, sizeof(buf), "LD C, d8"); return buf;
        case 0x11: return "LD DE, d16";
        case 0x16: std::snprintf(buf, sizeof(buf), "LD D, d8"); return buf;
        case 0x18: return "JR r8";
        case 0x19: return "ADD HL, DE";
        case 0x1E: std::snprintf(buf, sizeof(buf), "LD E, d8"); return buf;
        case 0x20: return "JR NZ, r8";
        case 0x21: return "LD HL, d16";
        case 0x22: return "LD (HL+), A";
        case 0x26: std::snprintf(buf, sizeof(buf), "LD H, d8"); return buf;
        case 0x28: return "JR Z, r8";
        case 0x29: return "ADD HL, HL";
        case 0x2A: return "LD A, (HL+)";
        case 0x2E: std::snprintf(buf, sizeof(buf), "LD L, d8"); return buf;
        case 0x30: return "JR NC, r8";
        case 0x31: return "LD SP, d16";
        case 0x32: return "LD (HL-), A";
        case 0x38: return "JR C, r8";
        case 0x39: return "ADD HL, SP";
        case 0x3A: return "LD A, (HL-)";
        case 0x3E: std::snprintf(buf, sizeof(buf), "LD A, d8"); return buf;

        case 0x04: return "INC B";
        case 0x0C: return "INC C";
        case 0x14: return "INC D";
        case 0x1C: return "INC E";
        case 0x24: return "INC H";
        case 0x2C: return "INC L";
        case 0x3C: return "INC A";

        case 0x05: return "DEC B";
        case 0x0D: return "DEC C";
        case 0x15: return "DEC D";
        case 0x1D: return "DEC E";
        case 0x25: return "DEC H";
        case 0x2D: return "DEC L";
        case 0x3D: return "DEC A";

        case 0x36: return "LD (HL), d8";

        case 0x76: return "HALT";

        case 0xC0: return "RET NZ";
        case 0xC1: return "POP BC";
        case 0xC2: return "JP NZ, a16";
        case 0xC3: return "JP a16";
        case 0xC4: return "CALL NZ, a16";
        case 0xC5: return "PUSH BC";
        case 0xC7: return "RST 00h";
        case 0xC8: return "RET Z";
        case 0xC9: return "RET";
        case 0xCA: return "JP Z, a16";
        case 0xCC: return "CALL Z, a16";
        case 0xCD: return "CALL a16";
        case 0xCF: return "RST 08h";
        case 0xD0: return "RET NC";
        case 0xD1: return "POP DE";
        case 0xD2: return "JP NC, a16";
        case 0xD4: return "CALL NC, a16";
        case 0xD5: return "PUSH DE";
        case 0xD7: return "RST 10h";
        case 0xD8: return "RET C";
        case 0xDA: return "JP C, a16";
        case 0xDC: return "CALL C, a16";
        case 0xDF: return "RST 18h";
        case 0xE1: return "POP HL";
        case 0xE5: return "PUSH HL";
        case 0xE7: return "RST 20h";
        case 0xEF: return "RST 28h";
        case 0xF1: return "POP AF";
        case 0xF5: return "PUSH AF";
        case 0xF7: return "RST 30h";
        case 0xFF: return "RST 38h";

        default:
            if (op >= 0x40 && op <= 0x7F) {
                int dst = (op >> 3) & 7;
                int src = op & 7;
                std::snprintf(buf, sizeof(buf), "LD %s, %s",
                              reg_name_by_index(dst), reg_name_by_index(src));
                return buf;
            }
            std::snprintf(buf, sizeof(buf), "DB %02X", op);
            return buf;
    }
}

void Cpu::push16(u16 v) {
    u8 hi = static_cast<u8>(v >> 8);
    u8 lo = static_cast<u8>(v & 0xFF);
    sp_ = static_cast<u16>(sp_ - 1);
    bus_.write8(sp_, hi);
    sp_ = static_cast<u16>(sp_ - 1);
    bus_.write8(sp_, lo);
}

u16 Cpu::pop16() {
    u8 lo = bus_.read8(sp_);
    sp_ = static_cast<u16>(sp_ + 1);
    u8 hi = bus_.read8(sp_);
    sp_ = static_cast<u16>(sp_ + 1);
    return static_cast<u16>((hi << 8) | lo);
}
