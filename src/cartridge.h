#pragma once
#include "types.h"
#include <vector>
#include <string>

class Cartridge {
public:
    enum class Mbc { None, Mbc1 };

    bool load(const std::string& path, std::string* err = nullptr);

    // Memory-mapped interface
    u8  read(u16 addr) const;   // 0000–7FFF and A000–BFFF
    void write(u16 addr, u8 val);

    Mbc mbc() const { return mbc_; }

private:
    // Helpers
    void parse_header();
    void allocate_ram_from_header(u8 ram_size_code);

    // MBC1 helpers
    size_t current_rom_bank_0000_3FFF() const;
    size_t current_rom_bank_4000_7FFF() const;
    size_t current_ram_bank() const;

private:
    std::vector<u8> rom_;     // full ROM image
    std::vector<u8> eram_;    // external RAM (0–128 KiB depending on cart)

    // Header-derived
    Mbc mbc_ = Mbc::None;
    bool has_battery_ = false;

    // MBC1 state
    bool ram_enabled_ = false;       // via 0000–1FFF (0x0A enables)
    u8   rom_bank_low5_ = 1;         // 1..31 (0 is remapped to 1)
    u8   bank_upper2_   = 0;         // high bits for ROM or RAM bank
    bool mode_rom_banking_ = true;   // 0=ROM banking (true here), 1=RAM banking

    // For convenience
    size_t rom_bank_size_ = 16 * 1024; // 16 KiB banks
    size_t ram_bank_size_ = 8  * 1024; // 8 KiB banks
};
