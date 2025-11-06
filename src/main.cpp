#include "bus.h"
#include "cpu.h"
#include "ppu.h"
#include "joypad.h"
#include "cartridge.h"

#include <SDL.h>
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>

static inline uint32_t rgb_to_argb8888(uint32_t rgb) {
    // fb pixels are 0xRRGGBB; SDL wants 0xAARRGGBB
    return 0xFF000000u | (rgb & 0x00FFFFFFu);
}

static void present_frame(SDL_Renderer* ren, SDL_Texture* tex, const std::vector<uint32_t>& fb) {
    // Convert to ARGB8888 and upload
    static std::vector<uint32_t> tmp;
    tmp.resize(fb.size());
    for (size_t i = 0; i < fb.size(); ++i) tmp[i] = rgb_to_argb8888(fb[i]);

    SDL_UpdateTexture(tex, nullptr, tmp.data(), 160 * sizeof(uint32_t));
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
}

int main(int argc, char** argv) {
    // --- Core objects
    Bus bus;
    Joypad jp;
    Cartridge cart;
    bus.attach_joypad(&jp);
    bus.attach_cartridge(&cart);

    // Try ROM
    if (argc >= 2) {
        std::string err;
        if (!cart.load(argv[1], &err)) {
            std::cerr << "ROM load error: " << err << "\n";
            return 1;
        }
        std::cout << "Loaded ROM: " << argv[1] << "\n";
    } else {
        std::cerr << "No ROM supplied. Booting with a tiny NOP loop.\n";
        // Small stub at 0x0100 so CPU has something to execute
        bus.write_block(0x0100, (const uint8_t*)"\xFB\x00", 2); // EI; NOP
    }

    // Minimal PPU setup (LCD on, BG on, tiledata @8000)
    bus.write8(0xFF40, (uint8_t)((1<<7)|(1<<4)|(1<<0))); // LCDC
    bus.write8(0xFF47, 0xE4); // BGP
    bus.write8(0xFF48, 0xE4); // OBP0
    bus.write8(0xFF49, 0xE4); // OBP1

    // Joypad: enable interrupts; select lines are controlled by game; we allow both selected (0)
    bus.write8(0xFFFF, bus.read8(0xFFFF) | 0x10); // IE bit4 for joypad

    Cpu cpu{bus};
    cpu.reset_no_bios();
    cpu.set_trace(false);

    Ppu ppu{bus};
    bus.attach_ppu(&ppu);

    // Let EI take effect if we used the stub
    { int c = cpu.step(); cpu.tick(c); ppu.tick(c); }

    // --- SDL init
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    const int scale = 4; // 160x144 -> 640x576
    SDL_Window* win = SDL_CreateWindow(
        "gbemu",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        160*scale, 144*scale,
        SDL_WINDOW_SHOWN);
    if (!win) { std::cerr << "SDL_CreateWindow: " << SDL_GetError() << "\n"; return 1; }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) { std::cerr << "SDL_CreateRenderer: " << SDL_GetError() << "\n"; return 1; }

    SDL_Texture* tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 160, 144);
    if (!tex) { std::cerr << "SDL_CreateTexture: " << SDL_GetError() << "\n"; return 1; }

    // Frame pacing
    const double target_fps = 59.73;
    const double frame_ms = 1000.0 / target_fps;

    bool running = true;
    uint64_t last_present_ms = SDL_GetTicks64();

    // Simple keyboard→joypad mapping:
    auto handle_key = [&](SDL_Keycode key, bool down) {
        switch (key) {
            case SDLK_RIGHT: jp.set(Button::Right,  down, &bus); break;
            case SDLK_LEFT:  jp.set(Button::Left,   down, &bus); break;
            case SDLK_UP:    jp.set(Button::Up,     down, &bus); break;
            case SDLK_DOWN:  jp.set(Button::Down,   down, &bus); break;
            case SDLK_z:     jp.set(Button::A,      down, &bus); break; // Z = A
            case SDLK_x:     jp.set(Button::B,      down, &bus); break; // X = B
            case SDLK_RETURN:jp.set(Button::Start,  down, &bus); break;
            case SDLK_RSHIFT:jp.set(Button::Select, down, &bus); break;
            default: break;
        }
    };

    std::vector<uint32_t> framebuf; // 160*144

    // Main loop
    while (running) {
        // --- Pump input
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            else if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = false;
                handle_key(ev.key.keysym.sym, true);
            } else if (ev.type == SDL_KEYUP) {
                handle_key(ev.key.keysym.sym, false);
            }
        }

        // --- Run CPU until VBlank begins; tick timers + PPU
        // (Your PPU sets frame_started() when entering Mode 1 at LY=144.)
        while (!ppu.frame_started()) {
            int c = cpu.step();
            cpu.tick(c);
            ppu.tick(c);
        }
        ppu.clear_frame_flag();

        // Grab the finished frame (built per-scanline in Step 14)
        ppu.copy_frame(framebuf);
        present_frame(ren, tex, framebuf);

        // --- Throttle to ~59.73 fps (basic)
        uint64_t now = SDL_GetTicks64();
        double elapsed = double(now - last_present_ms);
        if (elapsed < frame_ms) {
            SDL_Delay(static_cast<uint32_t>(frame_ms - elapsed));
            now = SDL_GetTicks64();
        }
        last_present_ms = now;
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
