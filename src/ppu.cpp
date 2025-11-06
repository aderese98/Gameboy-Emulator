#include "ppu.h"
#include "bus.h"
#include <algorithm>
#include <array>
#include <vector>
#include <iostream>

// Local helpers for tile fetch and palette mapping
namespace {
inline u8 map_palette(u8 pal, u8 ci) {
    int sh = (ci & 3) * 2;
    return static_cast<u8>((pal >> sh) & 3);
}

inline u8 fetch_tile_ci(const Bus& bus, bool tiledata_sel_8000, u8 tile_index, int x_in, int y_in) {
    u16 tile_addr;
    if (tiledata_sel_8000) {
        tile_addr = static_cast<u16>(0x8000 + tile_index * 16u);
    } else {
        i8 s = static_cast<i8>(tile_index);
        tile_addr = static_cast<u16>(0x9000 + s * 16);
    }
    u8 lo = bus.read8(static_cast<u16>(tile_addr + y_in * 2 + 0));
    u8 hi = bus.read8(static_cast<u16>(tile_addr + y_in * 2 + 1));
    u8 bit = static_cast<u8>(7 - (x_in & 7));
    u8 c0 = (lo >> bit) & 1;
    u8 c1 = (hi >> bit) & 1;
    return static_cast<u8>((c1 << 1) | c0);
}
} // namespace

static constexpr u16 IO_LCDC = 0xFF40;
static constexpr u16 IO_STAT = 0xFF41;
static constexpr u16 IO_SCY  = 0xFF42;
static constexpr u16 IO_SCX  = 0xFF43;
static constexpr u16 IO_LY   = 0xFF44;
static constexpr u16 IO_LYC  = 0xFF45;
static constexpr u16 IO_BGP  = 0xFF47;
static constexpr u16 IO_IF   = 0xFF0F;

// Bus may call this shim to let the PPU enforce write semantics for
// STAT/LY without Bus needing a PPU header dependency.
void ppu_io_write(Ppu* self, u16 addr, u8 val) {
    if (!self) return;
    switch (addr) {
        case Ppu::IO_STAT: {
            // Only bits 6..3 are writable. Bits 2..0 come from internal state.
            u8 cur = self->bus_.read8(Ppu::IO_STAT);
            u8 new_stat = static_cast<u8>((cur & 0x07) | (val & 0x78));
            // Mirror into bus memory and update internal stat copy high bits.
            self->bus_.write8(Ppu::IO_STAT, new_stat);
            self->stat_ = static_cast<u8>((self->stat_ & 0x07) | (val & 0x78));
            break;
        }
        case Ppu::IO_LY: {
            // Any write resets LY to 0 and triggers LY==LYC evaluation
            self->write_ly(0);
            self->check_lyc();
            break;
        }
        default: break;
    }
}

Ppu::Ppu(Bus& bus) : bus_(bus) {
    // Initialize IO mirrors
    lcd_on_ = (bus_.read8(IO_LCDC) & (1<<7)) != 0;
    write_ly(0);
    // Keep existing STAT value but normalize mode bits to 2 (OAM) for start
    u8 st = bus_.read8(IO_STAT);
    write_stat(static_cast<u8>((st & 0xF8) | 2)); // mode 2 on boot (OAM search)
    dots_in_line_ = 0;
    window_line_ = 0;
}

void Ppu::write_ly(u8 v) {
    ly_ = v;
    bus_.write8(IO_LY, ly_);
}

void Ppu::write_stat(u8 v) {
    // STAT: bits: 6=LYC=LY int, 5=Mode2 int, 4=Mode1 int, 3=Mode0 int, 2=LYC=LY flag, 1-0=mode
    stat_ = v;
    bus_.write8(IO_STAT, stat_);
}

void Ppu::raise_stat_if_enabled(u8 mask) {
    // If the corresponding enable bit is set in STAT, set IF STAT bit (bit 1)
    if (stat_ & mask) {
        u8 iff = bus_.read8(IO_IF);
        bus_.write8(IO_IF, static_cast<u8>(iff | (1<<1)));
    }
}

void Ppu::check_lyc() {
    u8 lyc = bus_.read8(IO_LYC);
    bool eq = (ly_ == lyc);

    // Update coincidence flag (bit 2) in STAT register
    if (eq) write_stat(static_cast<u8>(stat_ | (1<<2)));
    else    write_stat(static_cast<u8>(stat_ & ~(1<<2)));

    // Fire STAT LY==LYC interrupt only on rising edge when enabled (bit6)
    if (eq && !coincident_ && (stat_ & (1<<6))) {
        u8 iff = bus_.read8(IO_IF);
        bus_.write8(IO_IF, static_cast<u8>(iff | (1<<1)));
    }
    coincident_ = eq;
}

void Ppu::enter_mode(u8 mode) {
    // Set STAT mode bits
    write_stat( static_cast<u8>((stat_ & ~0x03) | (mode & 0x03)) );

    switch (mode) {
        case 2: // OAM
            // STAT mode 2 interrupt enable bit = bit 5
            raise_stat_if_enabled(1<<5);
            break;
        case 3: // Transfer
            // entering mode 3: no STAT edge
            break;
        case 0: { // HBlank
            // STAT mode 0 interrupt enable bit = bit 3
            raise_stat_if_enabled(1<<3);
            // NEW: render the just-finished visible line into the internal frame buffer
            if (lcd_on_ && ly_ <= 143) {
                render_scanline_fifo(static_cast<int>(ly_));
                // Window line counter increases only on lines where window is active
                u8 lcdc = bus_.read8(IO_LCDC);
                bool win_on = (lcdc & (1<<5)) != 0;
                if (win_on) {
                    u8 wy = bus_.read8(IO_WY);
                    u8 wx = bus_.read8(IO_WX);
                    int win_x = int(wx) - 7;
                    if (ly_ >= wy && win_x < 160) {
                        // Window was visible this line; advance its internal row
                        window_line_ = static_cast<u8>(window_line_ + 1);
                    }
                }
            }
            break;
        }
        case 1: { // VBlank
            // Raise VBlank IF bit 0
            {
                u8 iff = bus_.read8(IO_IF);
                bus_.write8(IO_IF, static_cast<u8>(iff | (1<<0)));
            }
            // STAT mode 1 interrupt enable bit = bit 4
            raise_stat_if_enabled(1<<4);
            // mark frame boundary (start of VBlank)
            frame_started_flag_ = true;
            // Reset Window line counter for next frame
            window_line_ = 0;
            break;
        }
    }
}

void Ppu::step_line_advance() {
    // Called when dots_in_line_ crosses mode boundaries or line end.
    // Determine mode based on dots_in_line_ and LY
    if (!lcd_on_) {
        // When LCD off, LY=0 and mode=0, not much happens
        write_ly(0);
        enter_mode(0);
        dots_in_line_ = 0;
        return;
    }

    // Check if we need to advance to next line
    if (dots_in_line_ >= DOTS_PER_LINE) {
        dots_in_line_ -= DOTS_PER_LINE;
        
        if (ly_ < 153) {
            write_ly(static_cast<u8>(ly_ + 1));
        } else {
            write_ly(0); // Wrap from line 153 to 0
        }
        check_lyc();
        
        // Set mode for the new line
        if (ly_ == 144) {
            enter_mode(1); // Entering VBlank
        } else if (ly_ >= 144) {
            enter_mode(1); // VBlank continues
        } else {
            enter_mode(2); // Start OAM search for visible line
        }
        
        return; // Process one line advance per call
    }

    // Handle mode transitions within a scanline (only for visible lines)
    if (ly_ <= 143) {
        int d = dots_in_line_;
        u8 current_mode = stat_ & 0x03;
        
        if (d < OAM_DOTS) {
            // Should be in mode 2 (OAM)
            if (current_mode != 2) enter_mode(2);
        } else if (d < OAM_DOTS + XFER_DOTS) {
            // Should be in mode 3 (pixel transfer)
            if (current_mode != 3) enter_mode(3);
        } else {
            // Should be in mode 0 (HBlank)
            if (current_mode != 0) enter_mode(0);
        }
    }
}

void Ppu::tick(int cycles) {
    // Sync writable STAT bits 6..3 from bus (game can change STAT enables).
    {
        u8 ext = bus_.read8(IO_STAT);
        stat_ = static_cast<u8>((stat_ & 0x07) | (ext & 0x78));
        // Write back a canonical STAT: keep our low3 (mode/coin), high from enables
        bus_.write8(IO_STAT, static_cast<u8>((stat_ & 0xF8) | (bus_.read8(IO_STAT) & 0x07)));
    }

    // Re-read LCDC each tick (games can toggle)
    u8 lcdc = bus_.read8(IO_LCDC);
    bool now_on = (lcdc & (1<<7)) != 0;
    if (now_on != lcd_on_) {
        lcd_on_ = now_on;
        dots_in_line_ = 0;
        write_ly(0);
        check_lyc();
        enter_mode(lcd_on_ ? 2 : 0);
    }
    if (!lcd_on_) return;

    // Advance dot counter
    int old_dots = dots_in_line_;
    dots_in_line_ += cycles;
    
    // Consume as many full lines as needed (no fixed "safety cap")
    while (dots_in_line_ >= DOTS_PER_LINE) {
        dots_in_line_ -= DOTS_PER_LINE;

        if (!lcd_on_) break; // safeguard: if LCD turned off while catching up

        if (ly_ < 143) {
            write_ly(static_cast<u8>(ly_ + 1));
            check_lyc();
            // entering a new visible line begins in Mode 2 (OAM)
            enter_mode(2);
        } else if (ly_ == 143) {
            // next is VBlank line 144
            write_ly(144);
            check_lyc();
            enter_mode(1); // VBlank (fires IF bit 0)
        } else if (ly_ < 153) {
            // lines 145..153 remain in VBlank (mode 1)
            write_ly(static_cast<u8>(ly_ + 1));
            check_lyc();
            enter_mode(1);
        } else {
            // wrap to line 0 (new frame)
            write_ly(0);
            check_lyc();
            enter_mode(2); // start next frame in Mode 2
        }
    }

    // Now handle intra-line mode edges that might be crossed in the *remaining* dots
    int d = dots_in_line_;
    if (ly_ <= 143) {
        if (d < OAM_DOTS) {
            if ((stat_ & 0x03) != 2) enter_mode(2);
        } else if (d < OAM_DOTS + XFER_DOTS) {
            if ((stat_ & 0x03) != 3) enter_mode(3);
        } else {
            if ((stat_ & 0x03) != 0) enter_mode(0);
        }
    } else {
        // still in VBlank; mode remains 1 until next line wrap
        if ((stat_ & 0x03) != 1) enter_mode(1);
    }
}

void Ppu::render_bg(std::vector<u32>& fb) {
    // Expect 160*144
    if (fb.size() < 160u * 144u) fb.resize(160u * 144u);

    u8 lcdc = bus_.read8(IO_LCDC);
    const bool lcd_on   = (lcdc & (1 << 7)) != 0;
    const bool bg_on    = (lcdc & (1 << 0)) != 0;
    const bool tiledata_sel_8000 = (lcdc & (1 << 4)) != 0; // 1=0x8000 unsigned, 0=0x8800 signed
    const bool bgmap_sel_9C00    = (lcdc & (1 << 3)) != 0; // 1=0x9C00, 0=0x9800

    u8 scy = bus_.read8(IO_SCY);
    u8 scx = bus_.read8(IO_SCX);
    u8 bgp = bus_.read8(IO_BGP); // DMG palette (2 bits per color index)

    if (!lcd_on || !bg_on) {
        // Fill black if LCD/BG disabled (simple behavior)
        std::fill(fb.begin(), fb.end(), 0x000000u);
        return;
    }

    const u16 tilemap_base = bgmap_sel_9C00 ? 0x9C00 : 0x9800;

    // Pre-decode BGP into shade mapping: color index 0..3 -> shade 0..3
    auto map_color = [&](u8 ci) -> u8 {
        int shift = static_cast<int>(ci) * 2;
        return static_cast<u8>((bgp >> shift) & 0b11);
    };

    for (int y = 0; y < 144; ++y) {
        u16 vy = static_cast<u16>(static_cast<u8>(y + scy));
        int tile_row = (vy / 8) & 31;

        for (int x = 0; x < 160; ++x) {
            u16 vx = static_cast<u16>(static_cast<u8>(x + scx));
            int tile_col = (vx / 8) & 31;

            u16 tile_index_addr = static_cast<u16>(tilemap_base + tile_row * 32 + tile_col);
            u8 tile_index = bus_.read8(tile_index_addr);

            u8 ci = 0;
            if (bg_on) {
                u8 line = static_cast<u8>(vy & 7);
                u8 ci_local = fetch_tile_ci(bus_, tiledata_sel_8000, tile_index, vx & 7, line);
                u8 shade = map_palette(bgp, ci_local);
                fb[y * 160 + x] = dmgb_shade_to_rgb(shade);
                ci = ci_local;
            } else {
                fb[y * 160 + x] = dmgb_shade_to_rgb(map_palette(bgp, 0));
                ci = 0;
            }
            // Note: this render_bg is independent; render_frame uses its own bg_ci
        }
    }
}

void Ppu::render_frame(std::vector<u32>& fb) {
    if (fb.size() < 160u * 144u) fb.resize(160u * 144u);

    u8 lcdc = bus_.read8(IO_LCDC);
    const bool lcd_on   = (lcdc & (1 << 7)) != 0;
    const bool win_on   = (lcdc & (1 << 5)) != 0;
    const bool bg_on    = (lcdc & (1 << 0)) != 0;
    const bool obj_on   = (lcdc & (1 << 1)) != 0;
    const bool tiledata_sel_8000 = (lcdc & (1 << 4)) != 0;
    const bool bgmap_sel_9C00    = (lcdc & (1 << 3)) != 0;
    const bool winmap_sel_9C00   = (lcdc & (1 << 6)) != 0;

    u8 scy = bus_.read8(IO_SCY);
    u8 scx = bus_.read8(IO_SCX);
    u8 bgp = bus_.read8(IO_BGP);
    u8 obp0 = bus_.read8(IO_OBP0);
    u8 obp1 = bus_.read8(IO_OBP1);
    u8 wy  = bus_.read8(IO_WY);
    u8 wx  = bus_.read8(IO_WX);

    if (!lcd_on) {
        std::fill(fb.begin(), fb.end(), 0x000000u);
        return;
    }

    const u16 bg_base   = bgmap_sel_9C00  ? 0x9C00 : 0x9800;
    const u16 win_base  = winmap_sel_9C00 ? 0x9C00 : 0x9800;

    // 1) Draw BG (or just a flat color if BG disabled)
    // Also keep a buffer of BG color indices (for OBJ priority)
    std::array<u8, 160*144> bg_ci{};
    for (int y = 0; y < 144; ++y) {
        u16 vy = static_cast<u16>(static_cast<u8>(y + scy));
        int tile_row = (vy / 8) & 31;

        for (int x = 0; x < 160; ++x) {
            u16 vx = static_cast<u16>(static_cast<u8>(x + scx));
            int tile_col = (vx / 8) & 31;
            u16 tile_index_addr = static_cast<u16>(bg_base + tile_row * 32 + tile_col);
            u8 tile_index = bus_.read8(tile_index_addr);

            u8 ci = 0;
            if (bg_on) {
                u8 line = static_cast<u8>(vy & 7);
                u8 ci_local = fetch_tile_ci(bus_, tiledata_sel_8000, tile_index, vx & 7, line);
                u8 shade = map_palette(bgp, ci_local);
                fb[y * 160 + x] = dmgb_shade_to_rgb(shade);
                ci = ci_local;
            } else {
                fb[y * 160 + x] = dmgb_shade_to_rgb(map_palette(bgp, 0));
                ci = 0;
            }
            bg_ci[y*160 + x] = ci;
        }
    }

    // 2) Window on top of BG
    if (win_on) {
        int win_x = static_cast<int>(wx) - 7;
        int win_y = static_cast<int>(wy);
        for (int y = 0; y < 144; ++y) {
            if (y < win_y) continue;
            int wy_in = y - win_y;
            int tile_row = (wy_in / 8) & 31;
            for (int x = 0; x < 160; ++x) {
                if (x < win_x) continue;
                int wx_in = x - win_x;
                int tile_col = (wx_in / 8) & 31;
                u16 taddr = static_cast<u16>(win_base + tile_row * 32 + tile_col);
                u8 t = bus_.read8(taddr);
                u8 ci_local = fetch_tile_ci(bus_, tiledata_sel_8000, t, wx_in & 7, wy_in & 7);
                bg_ci[y*160 + x] = ci_local;
                u8 shade = map_palette(bgp, ci_local);
                fb[y * 160 + x] = dmgb_shade_to_rgb(shade);
            }
        }
    }

    // 3) Sprites over BG/Window
    if (obj_on) {
        struct Sprite { int x,y; u8 tile; u8 attr; int oam_index; };
        std::vector<Sprite> line_sprites;
        line_sprites.reserve(40);

        for (int y = 0; y < 144; ++y) {
            line_sprites.clear();
            for (int i = 0; i < 40; ++i) {
                u16 base = static_cast<u16>(0xFE00 + i*4);
                u8 sy = bus_.read8(base + 0);
                u8 sx = bus_.read8(base + 1);
                u8 tile = bus_.read8(base + 2);
                u8 attr = bus_.read8(base + 3);
                int oy = static_cast<int>(sy) - 16;
                int ox = static_cast<int>(sx) - 8;
                if (oy <= y && (oy + 8) > y) {
                    line_sprites.push_back({ox, oy, tile, attr, i});
                }
            }
            std::stable_sort(line_sprites.begin(), line_sprites.end(),
                [](const Sprite& a, const Sprite& b){
                    if (a.x != b.x) return a.x < b.x;
                    return a.oam_index < b.oam_index;
                });
            if (line_sprites.size() > 10) line_sprites.resize(10);

            for (const Sprite& s : line_sprites) {
                bool priority_bg = (s.attr & 0x80) != 0;
                bool yflip = (s.attr & 0x40) != 0;
                bool xflip = (s.attr & 0x20) != 0;
                bool pal1  = (s.attr & 0x10) != 0;
                u8 pal = pal1 ? obp1 : obp0;

                int y_in = y - s.y;
                if (yflip) y_in = 7 - y_in;

                for (int x = std::max(0, s.x); x < std::min(160, s.x + 8); ++x) {
                    int x_in = x - s.x;
                    if (xflip) x_in = 7 - x_in;
                    u8 ci = fetch_tile_ci(bus_, /*tiledata*/true, s.tile, x_in, y_in);
                    if (ci == 0) continue;
                    if (priority_bg && bg_ci[y*160 + x] != 0) continue;
                    u8 shade = map_palette(pal, ci);
                    fb[y * 160 + x] = dmgb_shade_to_rgb(shade);
                }
            }
        }
    }
}

void Ppu::render_scanline_fifo(int y) {
    // IO & modes
    u8 lcdc = bus_.read8(IO_LCDC);
    const bool bg_on  = (lcdc & (1<<0)) != 0;
    const bool obj_on = (lcdc & (1<<1)) != 0;
    const bool win_on = (lcdc & (1<<5)) != 0;
    const bool tiledata_sel_8000 = (lcdc & (1<<4)) != 0;
    const bool bgmap_sel_9C00    = (lcdc & (1<<3)) != 0;
    const bool winmap_sel_9C00   = (lcdc & (1<<6)) != 0;
    const bool sprites_8x16      = (lcdc & (1<<2)) != 0;

    const u16 bg_base  = bgmap_sel_9C00  ? 0x9C00 : 0x9800;
    const u16 win_base = winmap_sel_9C00 ? 0x9C00 : 0x9800;

    u8 scy = bus_.read8(IO_SCY), scx = bus_.read8(IO_SCX);
    u8 bgp = bus_.read8(IO_BGP);
    u8 obp0 = bus_.read8(IO_OBP0), obp1 = bus_.read8(IO_OBP1);
    u8 wy = bus_.read8(IO_WY), wx = bus_.read8(IO_WX);

    const int win_x = int(wx) - 7;
    const bool win_line_active = win_on && (y >= wy) && (win_x < 160);

    u8 bg_ci_line[160];

    // BG with fine scroll
    if (bg_on) {
        u16 vy = static_cast<u16>(static_cast<u8>(y + scy));
        int tile_row = (vy / 8) & 31;
        int xFine = scx & 7;
        int tile_col = (scx / 8) & 31;

        u8 tile_index = bus_.read8(static_cast<u16>(bg_base + tile_row*32 + tile_col));
        u8 px8[8];
        for (int i = 0; i < 8; ++i) px8[i] = fetch_tile_ci(bus_, tiledata_sel_8000, tile_index, i, vy & 7);

        int x = 0;
        int i = xFine;
        while (x < 160) {
            for (; i < 8 && x < 160; ++i, ++x) {
                u8 ci = px8[i];
                bg_ci_line[x] = ci;
                u8 shade = map_palette(bgp, ci);
                fb_[y*160 + x] = dmgb_shade_to_rgb(shade);
            }
            i = 0;
            tile_col = (tile_col + 1) & 31;
            tile_index = bus_.read8(static_cast<u16>(bg_base + tile_row*32 + tile_col));
            for (int k = 0; k < 8; ++k) px8[k] = fetch_tile_ci(bus_, tiledata_sel_8000, tile_index, k, vy & 7);
        }
    } else {
        for (int x = 0; x < 160; ++x) {
            bg_ci_line[x] = 0;
            fb_[y*160 + x] = dmgb_shade_to_rgb(map_palette(bgp, 0));
        }
    }

    // Window overrides
    if (win_line_active) {
        int wy_in = window_line_;
        int tile_row = (wy_in / 8) & 31;
        for (int x = std::max(0, win_x); x < 160; ++x) {
            int wx_in = x - win_x;
            int tile_col = (wx_in / 8) & 31;
            u8 t = bus_.read8(static_cast<u16>(win_base + tile_row*32 + tile_col));
            u8 ci = fetch_tile_ci(bus_, tiledata_sel_8000, t, wx_in & 7, wy_in & 7);
            bg_ci_line[x] = ci;
            u8 shade = map_palette(bgp, ci);
            fb_[y*160 + x] = dmgb_shade_to_rgb(shade);
        }
    }

    // Sprites for the line
    if (obj_on) {
        struct S { int x,y; u8 tile, attr; int idx; };
        std::vector<S> line;
        line.reserve(40);

        for (int i = 0; i < 40; ++i) {
            u16 base = static_cast<u16>(0xFE00 + i*4);
            u8 sy = bus_.read8(base+0), sx = bus_.read8(base+1);
            u8 tile = bus_.read8(base+2), attr = bus_.read8(base+3);
            int oy = int(sy) - 16, ox = int(sx) - 8;
            int h = sprites_8x16 ? 16 : 8;
            if (oy <= y && y < oy + h) {
                if (sprites_8x16) tile &= 0xFE;
                line.push_back({ox, oy, tile, attr, i});
            }
        }

        std::stable_sort(line.begin(), line.end(), [](const S&a,const S&b){
            if (a.x != b.x) return a.x < b.x;
            return a.idx < b.idx;
        });
        if (line.size() > 10) line.resize(10);

        for (const S& s : line) {
            bool priority_bg = (s.attr & 0x80) != 0;
            bool yflip = (s.attr & 0x40) != 0;
            bool xflip = (s.attr & 0x20) != 0;
            bool pal1  = (s.attr & 0x10) != 0;
            u8 pal = pal1 ? obp1 : obp0;

            int h = sprites_8x16 ? 16 : 8;
            int y_in = y - s.y;
            if (yflip) y_in = (h-1) - y_in;

            u8 tile_for_line = s.tile;
            if (sprites_8x16) {
                bool bottom = (y_in >= 8);
                u8 base_tile = s.tile;
                if (yflip) bottom = !bottom;
                tile_for_line = static_cast<u8>(base_tile + (bottom ? 1 : 0));
                y_in &= 7;
            }

            for (int x = std::max(0, s.x); x < std::min(160, s.x + 8); ++x) {
                int x_in = x - s.x; if (xflip) x_in = 7 - x_in;
                u8 ci = fetch_tile_ci(bus_, /*tiledata_sel_8000=*/true, tile_for_line, x_in, y_in);
                if (ci == 0) continue;
                if (priority_bg && bg_ci_line[x] != 0) continue;
                u8 shade = map_palette(pal, ci);
                fb_[y*160 + x] = dmgb_shade_to_rgb(shade);
            }
        }
    }
}

void Ppu::copy_frame(std::vector<u32>& out) const {
    out.assign(fb_.begin(), fb_.end());
}

u32 Ppu::dmgb_shade_to_rgb(u8 s) {
    switch (s & 3) {
        case 0: return 0xFFFFFFu;
        case 1: return 0xC0C0C0u;
        case 2: return 0x606060u;
        default: return 0x000000u;
    }
}
