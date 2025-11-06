#include "bus.h"
#include "cpu.h"
#include <iostream>
#include <vector>

int main() {
    Bus bus;
    Cpu cpu{bus};
    cpu.reset_no_bios();
    cpu.set_trace(true);

    // Layout from 0x0100:
    // 0100: LD A,01
    // 0102: DEC A            ; A=00, Z=1
    // 0103: JR Z,+02         ; skip next LD B,99
    // 0105: LD B,99          ; (skipped)
    // 0107: NOP
    // 0108: LD B,42          ; B=42 (always)
    // 010A: LD HL,C100
    // 010D: LD (HL),B        ; [C100]=42
    // 010E: CALL 0112
    // 0111: HALT
    //
    // 0112: LD HL,C101
    // 0115: LD A,77
    // 0117: LD (HL),A        ; [C101]=77
    // 0118: RET
    const std::vector<u8> rom = {
        0x3E, 0x01,       // LD A,01
        0x3D,             // DEC A     -> Z=1
        0x28, 0x02,       // JR Z,+2   (skip LD B,99)
        0x06, 0x99,       // LD B,99   (skipped)
        0x00,             // NOP
        0x06, 0x42,       // LD B,42
        0x21, 0x00, 0xC1, // LD HL,C100
        0x70,             // LD (HL),B
        0xCD, 0x12, 0x01, // CALL 0112
        0x76,             // HALT
        // --- subroutine at 0x0112 ---
        0x21, 0x01, 0xC1, // LD HL,C101
        0x3E, 0x77,       // LD A,77
        0x77,             // LD (HL),A
        0xC9              // RET
    };

    bus.write_block(0x0100, rom.data(), rom.size());

    int total = 0;
    for (size_t i = 0; i < rom.size(); ++i) {
        total += cpu.step();
    }

    std::cout << "Final: " << cpu.dump() << "\n";
    std::cout << "Total cycles: " << total << "\n";
    std::cout << std::hex
              << "[C100]=" << (int)bus.read8(0xC100)
              << " [C101]=" << (int)bus.read8(0xC101)
              << std::dec << "\n";
    return 0;
}

