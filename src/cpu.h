#pragma once
#include "types.h"
#include <string>

class Bus; // forward declaration

// Flags (upper 4 bits of F)
enum Flag : u8 {
    FLAG_Z = 1 << 7, // Zero
    FLAG_N = 1 << 6, // Subtract
    FLAG_H = 1 << 5, // Half-carry
    FLAG_C = 1 << 4  // Carry
};

class Cpu {
public:
    explicit Cpu(Bus& bus);

    void reset_no_bios();
    std::string dump() const;

    // Execute one instruction at PC. Returns machine cycles consumed.
    int step();
    
    // Tracing control
    void set_trace(bool on) { trace_ = on; }

    // 16-bit register pairs
    u16 AF() const { return af_; }
    u16 BC() const { return bc_; }
    u16 DE() const { return de_; }
    u16 HL() const { return hl_; }
    u16 SP() const { return sp_; }
    u16 PC() const { return pc_; }

    // 8-bit high/low views
    u8  A() const { return static_cast<u8>(af_ >> 8); }
    u8  F() const { return static_cast<u8>(af_ & 0xFF); }
    u8  B() const { return static_cast<u8>(bc_ >> 8); }
    u8  C() const { return static_cast<u8>(bc_ & 0xFF); }
    u8  D() const { return static_cast<u8>(de_ >> 8); }
    u8  E() const { return static_cast<u8>(de_ & 0xFF); }
    u8  H() const { return static_cast<u8>(hl_ >> 8); }
    u8  L() const { return static_cast<u8>(hl_ & 0xFF); }

    void setA(u8 v) { af_ = static_cast<u16>((v << 8) | (af_ & 0x00FF)); }
    void setF(u8 v) { af_ = static_cast<u16>((af_ & 0xFF00) | (v & 0xF0)); } // lower nibble must be 0
    void setB(u8 v) { bc_ = static_cast<u16>((v << 8) | (bc_ & 0x00FF)); }
    void setC(u8 v) { bc_ = static_cast<u16>((bc_ & 0xFF00) | v); }
    void setD(u8 v) { de_ = static_cast<u16>((v << 8) | (de_ & 0x00FF)); }
    void setE(u8 v) { de_ = static_cast<u16>((de_ & 0xFF00) | v); }
    void setH(u8 v) { hl_ = static_cast<u16>((v << 8) | (hl_ & 0x00FF)); }
    void setL(u8 v) { hl_ = static_cast<u16>((hl_ & 0xFF00) | v); }

    // 16-bit setters
    void setBC(u16 v) { bc_ = v; }
    void setDE(u16 v) { de_ = v; }
    void setHL(u16 v) { hl_ = v; }
    void setSP(u16 v) { sp_ = v; }

private:
    // --- helpers ---
    u8  fetch8();
    u16 fetch16(); // little-endian: lo then hi

    void set_flag(Flag f, bool on);
    bool get_flag(Flag f) const;

    // INC/DEC helpers for 8-bit regs with proper flags
    u8  alu_inc8(u8 v);
    u8  alu_dec8(u8 v);

    // Small helpers to map opcodes to registers
    void set_reg_by_index(int idx, u8 v); // idx: 0=B,1=C,2=D,3=E,4=H,5=L,6=(HL not used here),7=A
    u8   get_reg_by_index(int idx) const;

    // Disassembler for a small subset
    std::string disasm(u16 pc_before, u8 op) const;

    // stack helpers
    void push16(u16 v);
    u16  pop16();

    // condition helpers
    bool condNZ() const { return !get_flag(FLAG_Z); }
    bool condZ()  const { return  get_flag(FLAG_Z); }
    bool condNC() const { return !get_flag(FLAG_C); }
    bool condC()  const { return  get_flag(FLAG_C); }

    static i8  as_i8(u8 b) { return static_cast<i8>(b); }

private:
    Bus& bus_;
    u16 af_{};
    u16 bc_{};
    u16 de_{};
    u16 hl_{};
    u16 sp_{};
    u16 pc_{};

    bool trace_{true};
};
