#include "bus.h"
#include "cpu.h"
#include "ppu.h"
#include "joypad.h"
#include "cartridge.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

static void save_ppm_rgb888(const std::string& path, const std::vector<u32>& fb) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n160 144\n255\n";
    for (u32 rgb : fb) {
        unsigned char r = (rgb >> 16) & 0xFF;
        unsigned char g = (rgb >>  8) & 0xFF;
        unsigned char b = (rgb >>  0) & 0xFF;
        f.write((char*)&r,1); f.write((char*)&g,1); f.write((char*)&b,1);
    }
}

int main(int argc, char** argv) {
    Bus bus;
    Joypad jp;
    Cartridge cart;
    bus.attach_joypad(&jp);
    bus.attach_cartridge(&cart);

    // Try to load ROM, else build a dummy 32 KiB MBC0 ROM that just NOPs
    if (argc >= 2) {
        std::string err;
        if (!cart.load(argv[1], &err)) {
            std::cerr << "ROM load error: " << err << "\n";
            return 1;
        }
        std::cout << "Loaded ROM: " << argv[1] << "\n";
    } else {
        // Dummy ROM: 32 KiB of 0x00, with a reset entry that NOPs forever
        std::vector<u8> dummy(32 * 1024, 0x00);
        // Optional: put a JP 0x0100 at 0x0000 to mimic no-BIOS jump (not necessary here)
        cart = Cartridge(); // reset state
        // Hacky in-memory "load": assign directly (not via load()), enough for MBC0
        // We'll piggyback: write to Bus's memory for the reset vector area,
        // but reads come from cart; to keep it simple, instead write to a temp file-less load:
        // We'll expose a quick shim: since we didn't write such a function, fallback:
        std::cerr << "No ROM supplied. Running with a dummy MBC0 ROM (all NOPs).\n";
        // Minimal trick: write NOPs into ROM area by cart read path being null would return 0xFF.
        // To keep behavior deterministic, we just continue; CPU will fetch 0x00 after PC=0100 if we seed bus?
        // Simpler: install a tiny ROM-like program into RAM at 0x0100 as before:
        bus.write_block(0x0100, (const u8*)"\x00\x00\x00\x76", 4); // NOP NOP NOP HALT
    }

    Cpu cpu{bus};
    cpu.reset_no_bios();
    cpu.set_trace(false);

    // Minimal BG seed (same as Step 9)
    const u16 IO_LCDC = 0xFF40, IO_SCY=0xFF42, IO_SCX=0xFF43, IO_BGP=0xFF47;
    auto write_tile = [&](u16 base, u8 idx, const u8 lines[8][2]) {
        u16 addr = base + idx*16u;
        for (int y=0;y<8;++y){ bus.write8(addr+y*2+0, lines[y][0]); bus.write8(addr+y*2+1, lines[y][1]); }
    };
    u16 tiledata_base = 0x8000;
    { u8 lines[8][2] = {}; write_tile(tiledata_base, 0, lines); }
    {
        u8 lines[8][2];
        for (int y=0;y<8;++y){ lines[y][0]=0b10101010; lines[y][1]=0b01010101; }
        write_tile(tiledata_base, 1, lines);
    }
    u16 bgmap = 0x9800;
    for (int ty=0; ty<32; ++ty) for (int tx=0; tx<32; ++tx) {
        u8 t = ((tx ^ ty) & 1) ? 1 : 0;
        bus.write8(bgmap + ty*32 + tx, t);
    }
    
    // Enable sprites & window in LCDC
    u8 lcdc_val = (1<<7)|(1<<4)|(1<<0); // LCD on, tile data 0x8000, BG on
    lcdc_val |= (1<<1); // OBJ enable
    lcdc_val |= (1<<5); // Window enable (for testing)
    bus.write8(IO_LCDC, lcdc_val);
    // Clear LYC coincidence to avoid stuck STAT bit at init
    bus.write8(0xFF45, 0x00);

    // Now that LCDC/LYC are initialized, construct the PPU so it sees the correct state
    Ppu ppu{bus};
    
    bus.write8(IO_SCX, 0x00);
    bus.write8(IO_SCY, 0x00);
    bus.write8(IO_BGP, 0xE4);

    // Also initialize STAT for interrupts (enable VBlank mode interrupt)
    bus.write8(0xFF41, 0x10); // Enable Mode 1 (VBlank) interrupt

    // Set sprite palettes
    bus.write8(0xFF48, 0xE4); // OBP0
    bus.write8(0xFF49, 0xE4); // OBP1

    // Place one sprite using tile 1 (your checker) at (80,80)
    // OAM entry format: [y+16, x+8, tile, attr]
    u16 oam = 0xFE00;
    bus.write8(oam + 0, 80 + 16); // Y
    bus.write8(oam + 1, 80 + 8);  // X
    bus.write8(oam + 2, 1);       // checker tile index
    bus.write8(oam + 3, 0x00);    // attr: priority=0, no flip, OBP0

    // Window: show a small patch starting at (WX=40+7, WY=40)
    bus.write8(0xFF4B, 40 + 7); // WX
    bus.write8(0xFF4A, 40);     // WY

    // Joypad: enable interrupt and select both lines for demo reads
    // Also enable VBlank interrupt (IE bit 0)
    bus.write8(0xFFFF, 0x11); // VBlank (bit 0) + Joypad (bit 4)
    bus.write8(0xFF00, 0x00);

    // Enable interrupts globally via a tiny 2-byte ROM/RAM stub (EI; NOP; NOP; NOP loop)
    bus.write_block(0x0100, (const u8*)"\xFB\x00\x00\x18\xFC", 5); // EI; NOP; NOP; JR -4 (loop)
    
    // VBlank ISR at 0x0040: just RETI
    bus.write_block(0x0040, (const u8*)"\xD9", 1); // RETI
    
    int cyc0 = cpu.step(); cpu.tick(cyc0); ppu.tick(cyc0);

    std::vector<u32> fb(160*144);
    int frame_no = 0;
    int total_cycles = 0;

    std::cout << "Starting main loop...\n";
    std::cout << "Initial LCDC=" << std::hex << (int)bus.read8(0xFF40) << std::dec 
              << ", LY=" << (int)bus.read8(0xFF44) << ", STAT=" << std::hex << (int)bus.read8(0xFF41) << std::dec << "\n";

    // Simplified: just run cycle-accurate ticking for ~3 frames worth of cycles  
    constexpr int CYCLES_PER_FRAME = 70224;
    constexpr int MAX_CYCLES = CYCLES_PER_FRAME * 4; // 4 frames worth

    while (total_cycles < MAX_CYCLES && frame_no < 3) {
        int c = cpu.step();
        cpu.tick(c);
        ppu.tick(c);
        total_cycles += c;

        // Debug periodically (every 5000 cycles instead of 50000)
        if (total_cycles % 5000 < 20 && total_cycles > 1000) {
            std::cout << "Cycles: " << total_cycles << ", LY=" << (int)bus.read8(0xFF44) 
                      << ", STAT=" << std::hex << (int)bus.read8(0xFF41) << std::dec
                      << ", frame_started=" << (ppu.frame_started() ? "YES" : "no") << "\n";
        }

        // When we *enter* VBlank, we can render a frame snapshot
        if (ppu.frame_started()) {
            ppu.clear_frame_flag();
            ppu.render_frame(fb);   // BG + Window + Sprites
            save_ppm_rgb888("timed_" + std::to_string(frame_no) + ".ppm", fb);
            std::cout << "Wrote timed_" << frame_no << ".ppm (cycles: " << total_cycles << ")\n";
            ++frame_no;
        }
    }

    std::cout << "Done. Rendered " << frame_no << " frames.\n";
    return 0;
}
