#pragma once
#include "types.h"
#include <array>
#include <cstddef> // std::size_t

class Joypad;
class Cartridge;
class Ppu;

class Bus {
public:
    Bus();

    u8  read8(u16 addr) const;
    void write8(u16 addr, u8 val);

    void write_block(u16 start, const u8* data, std::size_t len);

    // NEW
    void attach_joypad(Joypad* jp) { joypad_ = jp; }
    void attach_cartridge(Cartridge* cart) { cart_ = cart; }
    void attach_ppu(Ppu* p)          { ppu_ = p; }

private:
    std::array<u8, 0x10000> mem_;
    Joypad*   joypad_{nullptr};
    Cartridge* cart_{nullptr};
    Ppu*      ppu_{nullptr};
};
