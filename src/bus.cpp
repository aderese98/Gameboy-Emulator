#include "bus.h"
#include "joypad.h"
#include "cartridge.h"

static constexpr u16 IO_JOYP = 0xFF00;
static constexpr u16 IO_DMA  = 0xFF46;
static constexpr u16 OAM_BASE = 0xFE00;

Bus::Bus() : mem_{} {}

u8 Bus::read8(u16 addr) const {
    // Cartridge regions
    if ((addr < 0x8000) || (addr >= 0xA000 && addr <= 0xBFFF)) {
        if (cart_) return cart_->read(addr);
    }

    if (addr == IO_JOYP && joypad_) {
        return joypad_->read();
    }
    return mem_[addr];
}

void Bus::write8(u16 addr, u8 val) {
    // Cartridge control/ERAM
    if ((addr < 0x8000) || (addr >= 0xA000 && addr <= 0xBFFF)) {
        if (cart_) { cart_->write(addr, val); return; }
    }

    if (addr == IO_JOYP && joypad_) {
        joypad_->write(val);
        mem_[addr] = static_cast<u8>((mem_[addr] & ~0x30) | (val & 0x30));
        return;
    }

    if (addr == IO_DMA) {
        // Start OAM DMA: source page val*0x100 -> FE00..FE9F (160 bytes)
        u16 src = static_cast<u16>(val) * 0x100u;
        for (int i = 0; i < 160; ++i) {
            mem_[static_cast<u16>(OAM_BASE + i)] = read8(static_cast<u16>(src + i));
        }
        mem_[addr] = val;
        return;
    }

    mem_[addr] = val;
}

void Bus::write_block(u16 start, const u8* data, std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
        mem_[static_cast<u16>(start + i)] = data[i];
    }
}
