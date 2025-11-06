#include "cartridge.h"
#include <fstream>
#include <cstring>
#include <cassert>

static constexpr u16 HDR_CARTRIDGE_TYPE = 0x0147;
static constexpr u16 HDR_ROM_SIZE       = 0x0148;
static constexpr u16 HDR_RAM_SIZE       = 0x0149;

bool Cartridge::load(const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "Could not open ROM file: " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    std::streamsize n = f.tellg();
    if (n <= 0) {
        if (err) *err = "Empty ROM file";
        return false;
    }
    rom_.resize(static_cast<size_t>(n));
    f.seekg(0, std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(rom_.data()), n)) {
        if (err) *err = "Failed reading ROM bytes";
        return false;
    }

    parse_header();
    return true;
}

void Cartridge::parse_header() {
    auto byte_at = [&](u16 off) -> u8 {
        if (off < rom_.size()) return rom_[off];
        return 0;
    };

    const u8 cart_type = byte_at(HDR_CARTRIDGE_TYPE);
    const u8 rom_size  = byte_at(HDR_ROM_SIZE);
    const u8 ram_size  = byte_at(HDR_RAM_SIZE);

    // Basic mapper detect (enough for a lot of GB games/homebrew)
    // 0x00 = ROM ONLY
    // 0x01/0x02/0x03 = MBC1 (no RAM / RAM / RAM+BATTERY)
    if (cart_type == 0x00) {
        mbc_ = Mbc::None;
    } else if (cart_type == 0x01 || cart_type == 0x02 || cart_type == 0x03) {
        mbc_ = Mbc::Mbc1;
        has_battery_ = (cart_type == 0x03);
    } else {
        // Treat unknown as MBC0 with a warning-compatible behavior
        mbc_ = Mbc::None;
    }

    allocate_ram_from_header(ram_size);

    // MBC1 defaults
    rom_bank_low5_ = 1;
    bank_upper2_ = 0;
    mode_rom_banking_ = true;
    ram_enabled_ = false;
}

void Cartridge::allocate_ram_from_header(u8 ram_size_code) {
    // 0: none, 1: 2KB (MBC2 only), 2: 8KB (1 bank), 3: 32KB (4 banks),
    // 4: 128KB (16 banks), 5: 64KB (8 banks)
    size_t bytes = 0;
    switch (ram_size_code) {
        case 0: bytes = 0; break;
        case 1: bytes = 2 * 1024; break;    // (rare outside MBC2; we still allow)
        case 2: bytes = 8 * 1024; break;
        case 3: bytes = 32 * 1024; break;
        case 4: bytes = 128 * 1024; break;
        case 5: bytes = 64 * 1024; break;
        default: bytes = 0; break;
    }
    eram_.assign(bytes, 0xFF); // typical power-on fill
}

// ----------------- Addressed read/write -----------------

u8 Cartridge::read(u16 addr) const {
    if (addr < 0x8000) {
        // ROM area
        if (mbc_ == Mbc::None) {
            size_t i = addr;
            if (i < rom_.size()) return rom_[i];
            return 0xFF;
        } else { // MBC1
            if (addr < 0x4000) {
                size_t bank = current_rom_bank_0000_3FFF();
                size_t base = bank * rom_bank_size_;
                size_t i = base + addr;
                return (i < rom_.size()) ? rom_[i] : 0xFF;
            } else {
                size_t bank = current_rom_bank_4000_7FFF();
                size_t base = bank * rom_bank_size_;
                size_t i = base + (addr - 0x4000);
                return (i < rom_.size()) ? rom_[i] : 0xFF;
            }
        }
    } else if (addr >= 0xA000 && addr <= 0xBFFF) {
        // External RAM
        if (eram_.empty() || !ram_enabled_) return 0xFF;
        size_t bank = current_ram_bank();
        size_t base = bank * ram_bank_size_;
        size_t i = base + (addr - 0xA000);
        return (i < eram_.size()) ? eram_[i] : 0xFF;
    }

    return 0xFF; // not handled here
}

void Cartridge::write(u16 addr, u8 val) {
    if (mbc_ == Mbc::None) {
        // No writable ROM; only ERAM if present (some carts are RAM-only games)
        if (addr >= 0xA000 && addr <= 0xBFFF && !eram_.empty()) {
            size_t i = addr - 0xA000;
            if (i < eram_.size()) eram_[i] = val;
        }
        return;
    }

    // MBC1 control registers live in 0000–7FFF space
    if (addr <= 0x1FFF) {
        // RAM enable: lower nibble == 0x0A enables, anything else disables
        ram_enabled_ = ((val & 0x0F) == 0x0A);
    } else if (addr <= 0x3FFF) {
        // ROM bank low 5 bits; writing 0 maps to bank 1 (quirk)
        rom_bank_low5_ = static_cast<u8>(val & 0x1F);
        if (rom_bank_low5_ == 0) rom_bank_low5_ = 1;
    } else if (addr <= 0x5FFF) {
        // Upper 2 bits of ROM bank, or RAM bank (in RAM mode)
        bank_upper2_ = static_cast<u8>(val & 0x03);
    } else if (addr <= 0x7FFF) {
        // Mode select: 0=ROM banking (0000–3FFF fixed), 1=RAM banking (0000–3FFF uses upper bits)
        mode_rom_banking_ = ((val & 0x01) == 0);
        // (We choose 'true' to mean ROM banking for easier naming.)
    } else if (addr >= 0xA000 && addr <= 0xBFFF) {
        // ERAM write (if enabled)
        if (eram_.empty() || !ram_enabled_) return;
        size_t bank = current_ram_bank();
        size_t base = bank * ram_bank_size_;
        size_t i = base + (addr - 0xA000);
        if (i < eram_.size()) eram_[i] = val;
    }
}

size_t Cartridge::current_rom_bank_0000_3FFF() const {
    if (mbc_ != Mbc::Mbc1) return 0;
    // In ROM banking mode, bank 0 is fixed
    // In RAM banking mode, bank = (upper2 << 5) * 16KiB (i.e., 0, 32, 64, 96)
    if (mode_rom_banking_) {
        return 0;
    } else {
        return static_cast<size_t>(bank_upper2_) << 5;
    }
}

size_t Cartridge::current_rom_bank_4000_7FFF() const {
    if (mbc_ != Mbc::Mbc1) return 1;
    // Bank number: (upper2 << 5) | low5
    size_t bank = (static_cast<size_t>(bank_upper2_) << 5) | (rom_bank_low5_ & 0x1F);
    // Some ROM sizes mirror banks; clamp to available bank count:
    size_t banks = rom_.size() / rom_bank_size_;
    if (banks == 0) return 1;
    bank %= banks;
    if (bank == 0) bank = 1; // hardware remap quirk
    return bank;
}

size_t Cartridge::current_ram_bank() const {
    if (mbc_ != Mbc::Mbc1) return 0;
    if (mode_rom_banking_) {
        return 0;
    } else {
        return bank_upper2_ & 0x03;
    }
}
