#pragma once
#include "types.h"
#include <array>
#include <cstddef> // std::size_t

class Bus {
public:
    Bus();

    u8  read8(u16 addr) const;
    void write8(u16 addr, u8 val);

    // Convenience for tests/demo: write a block of bytes into memory.
    void write_block(u16 start, const u8* data, std::size_t len);

private:
    std::array<u8, 0x10000> mem_;
};
