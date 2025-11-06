#pragma once
#include "types.h"
#include <vector>

class Bus;

// Minimal PPU pass: render just the BG layer into an RGB framebuffer (160x144).
class Ppu {
public:
    explicit Ppu(Bus& bus) : bus_(bus) {}

    // Render one whole frame (BG only) into fb as 0xRRGGBB pixels; fb size must be 160*144.
    void render_bg(std::vector<u32>& fb);

private:
    Bus& bus_;

    // Helpers
    static u32 dmgb_shade_to_rgb(u8 shade); // 0..3 -> 24-bit RGB
};
