#pragma once
#include "types.h"
#include <vector>
#include <array>

class Bus;

class Ppu {
public:
    explicit Ppu(Bus& bus);

    // Existing:
    void render_bg(std::vector<u32>& fb);

    // NEW: full BG + Window + Sprites composite
    void render_frame(std::vector<u32>& fb);

    // Timing:
    void tick(int cycles);
    bool frame_started() const { return frame_started_flag_; }
    void clear_frame_flag()     { frame_started_flag_ = false; }

    // copy the internallyt built frame after VBlank start
    void copy_frame(std::vector<u32>& out) const;

private:
    Bus& bus_;

    // Timing constants/state 
    static constexpr int DOTS_PER_LINE = 456;
    static constexpr int OAM_DOTS = 80, XFER_DOTS = 172, HBLANK_DOTS = 204;
    int  dots_in_line_ = 0;
    u8   ly_ = 0;
    u8   stat_ = 0;
    bool lcd_on_ = false;
    bool frame_started_flag_ = false;
    // Last LY==LYC state — used to fire STAT only on rising edge
    bool coincident_ = false;

    // per-frame/line state
    std::array<u32, 160 * 144> fb_{};
    u8 window_line_ = 0;

    // Timing helpers
    void write_ly(u8 v);
    void write_stat(u8 v);
    void enter_mode(u8 mode);
    void check_lyc();
    void step_line_advance();
    void raise_stat_if_enabled(u8 mask);

    // per-line renderer (FIFO-lite)
    void render_scanline_fifo(int y);

    // IO addrs
    static constexpr u16 IO_LCDC = 0xFF40;
    static constexpr u16 IO_STAT = 0xFF41;
    static constexpr u16 IO_SCY  = 0xFF42;
    static constexpr u16 IO_SCX  = 0xFF43;
    static constexpr u16 IO_LY   = 0xFF44;
    static constexpr u16 IO_LYC  = 0xFF45;
    static constexpr u16 IO_BGP  = 0xFF47;
    static constexpr u16 IO_OBP0 = 0xFF48; // NEW
    static constexpr u16 IO_OBP1 = 0xFF49; // NEW
    static constexpr u16 IO_WY   = 0xFF4A; // NEW
    static constexpr u16 IO_WX   = 0xFF4B; // NEW
    static constexpr u16 IO_IF   = 0xFF0F;

    // Color mapping
    static u32 dmgb_shade_to_rgb(u8 shade);
    // Bus-level shim declared so Bus can call into PPU without including
    // the full class layout. Defined in src/ppu.cpp.
    friend void ppu_io_write(Ppu* self, u16 addr, u8 val);
};

// Simple C-style hook Bus can call without including full class layout:
void ppu_io_write(Ppu* self, u16 addr, u8 val);
