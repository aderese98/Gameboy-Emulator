#include "ppu.h"
#include "bus.h"

static constexpr u16 IO_LCDC = 0xFF40;
static constexpr u16 IO_STAT = 0xFF41; // (unused this step)
static constexpr u16 IO_SCY  = 0xFF42;
static constexpr u16 IO_SCX  = 0xFF43;
static constexpr u16 IO_BGP  = 0xFF47;

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
        // BGP bits: [7:6]=color3, [5:4]=color2, [3:2]=color1, [1:0]=color0
        int shift = static_cast<int>(ci) * 2;
        return static_cast<u8>((bgp >> shift) & 0b11);
    };

    for (int y = 0; y < 144; ++y) {
        u16 vy = static_cast<u16>(static_cast<u8>(y + scy)); // wrap 0..255
        int tile_row = (vy / 8) & 31; // 32 tiles per row

        for (int x = 0; x < 160; ++x) {
            u16 vx = static_cast<u16>(static_cast<u8>(x + scx));
            int tile_col = (vx / 8) & 31;

            u16 tile_index_addr = static_cast<u16>(tilemap_base + tile_row * 32 + tile_col);
            u8 tile_index = bus_.read8(tile_index_addr);

            // Compute tile pattern address
            u16 tile_addr;
            if (tiledata_sel_8000) {
                // Unsigned: 0x8000 + index*16
                tile_addr = static_cast<u16>(0x8000 + tile_index * 16u);
            } else {
                // Signed:   0x9000 + (int8_t(index))*16
                i8 s = static_cast<i8>(tile_index);
                tile_addr = static_cast<u16>(0x9000 + s * 16);
            }

            u8 line = static_cast<u8>(vy & 7); // 0..7 inside tile row
            u8 lo = bus_.read8(static_cast<u16>(tile_addr + line * 2 + 0));
            u8 hi = bus_.read8(static_cast<u16>(tile_addr + line * 2 + 1));

            // Pixel bit index: leftmost pixel is bit7
            u8 bit = static_cast<u8>(7 - (vx & 7));
            u8 c0 = (lo >> bit) & 1;
            u8 c1 = (hi >> bit) & 1;
            u8 color_index = static_cast<u8>((c1 << 1) | c0); // 0..3

            u8 shade = map_color(color_index); // 0..3 after palette
            fb[y * 160 + x] = dmgb_shade_to_rgb(shade);
        }
    }
}

u32 Ppu::dmgb_shade_to_rgb(u8 s) {
    // DMG shades from light (0) to dark (3). We'll map to neutral grays.
    switch (s & 3) {
        case 0: return 0xFFFFFFu; // white
        case 1: return 0xC0C0C0u; // light gray
        case 2: return 0x606060u; // dark gray
        default:return 0x000000u; // black
    }
}
