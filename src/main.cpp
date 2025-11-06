#include "bus.h"
#include "cpu.h"
#include "ppu.h"
#include <iostream>
#include <fstream>
#include <vector>

static void save_ppm_rgb888(const char* path, const std::vector<u32>& fb) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n160 144\n255\n";
    for (size_t i = 0; i < 160u * 144u; ++i) {
        u32 rgb = fb[i];
        unsigned char r = static_cast<unsigned char>((rgb >> 16) & 0xFF);
        unsigned char g = static_cast<unsigned char>((rgb >>  8) & 0xFF);
        unsigned char b = static_cast<unsigned char>((rgb >>  0) & 0xFF);
        f.write(reinterpret_cast<char*>(&r), 1);
        f.write(reinterpret_cast<char*>(&g), 1);
        f.write(reinterpret_cast<char*>(&b), 1);
    }
}

int main() {
    Bus bus;
    Cpu cpu{bus};
    cpu.reset_no_bios();
    cpu.set_trace(false);

    Ppu ppu{bus};

    // --- IO addresses ---
    const u16 IO_LCDC = 0xFF40;
    const u16 IO_SCY  = 0xFF42;
    const u16 IO_SCX  = 0xFF43;
    const u16 IO_BGP  = 0xFF47;

    // --- Seed VRAM with a tiny tileset ---
    // Tile 0: solid color index 0 (all zero bits)
    // Tile 1: checkerboard (alternating pixels per line)
    auto write_tile = [&](u16 base, u8 idx, const u8 lines[8][2]) {
        u16 addr = static_cast<u16>(base + idx * 16u);
        for (int y = 0; y < 8; ++y) {
            bus.write8(static_cast<u16>(addr + y*2 + 0), lines[y][0]);
            bus.write8(static_cast<u16>(addr + y*2 + 1), lines[y][1]);
        }
    };

    u16 tiledata_base = 0x8000; // Use unsigned mode (LCDC bit4=1)
    // Solid 0: all 0
    {
        u8 lines[8][2] = {};
        write_tile(tiledata_base, 0, lines);
    }
    // Checker: bit pairs make 0/3 stripes (we'll set BGP to map 0..3 nicely)
    {
        u8 lines[8][2];
        for (int y = 0; y < 8; ++y) {
            // pattern 10101010 for lo, 01010101 for hi gives alternating colors
            lines[y][0] = 0b10101010; // low bits
            lines[y][1] = 0b01010101; // high bits
        }
        write_tile(tiledata_base, 1, lines);
    }

    // --- Fill BG map (0x9800) with 32x32 tiles alternating 0 and 1
    u16 bgmap = 0x9800;
    for (int ty = 0; ty < 32; ++ty) {
        for (int tx = 0; tx < 32; ++tx) {
            u8 t = static_cast<u8>(((tx ^ ty) & 1) ? 1 : 0);
            bus.write8(static_cast<u16>(bgmap + ty * 32 + tx), t);
        }
    }

    // --- Set useful registers ---
    // LCDC: LCD on (bit7), BG tile map 0x9800 (bit3=0), tile data 0x8000 (bit4=1), BG enable (bit0=1)
    bus.write8(IO_LCDC, static_cast<u8>((1<<7) | (1<<4) | (1<<0)));
    bus.write8(IO_SCX, 0x00);
    bus.write8(IO_SCY, 0x00);
    // BGP palette: map color indices 0..3 to shades 0..3 in order => 0b11100100 (0xE4) or 0xE4/0x9B as you like.
    bus.write8(IO_BGP, 0xE4);

    // --- Render one frame (BG only) ---
    std::vector<u32> fb(160 * 144);
    ppu.render_bg(fb);

    save_ppm_rgb888("frame.ppm", fb);
    std::cout << "Wrote frame.ppm (160x144).\n";
    return 0;
}
