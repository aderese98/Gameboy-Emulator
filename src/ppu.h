#pragma once
#include "types.h"
#include <vector>

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

private:
    Bus& bus_;

    // Timing constants/state (unchanged from Step 12)
    static constexpr int DOTS_PER_LINE = 456;
    static constexpr int OAM_DOTS = 80, XFER_DOTS = 172, HBLANK_DOTS = 204;
    int  dots_in_line_ = 0;
    u8   ly_ = 0;
    u8   stat_ = 0;
    bool lcd_on_ = false;
    bool frame_started_flag_ = false;

    // Timing helpers (unchanged)
    void write_ly(u8 v);
    void write_stat(u8 v);
    void enter_mode(u8 mode);
    void check_lyc();
    void step_line_advance();
    void raise_stat_if_enabled(u8 mask);

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
};
