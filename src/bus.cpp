#include "bus.h"

Bus::Bus() : mem_{} {}

u8 Bus::read8(u16 addr) const {
    return mem_[addr];
}

void Bus::write8(u16 addr, u8 val) {
    mem_[addr] = val;
}

void Bus::write_block(u16 start, const u8* data, std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
        mem_[start + static_cast<u16>(i)] = data[i];
    }
}
