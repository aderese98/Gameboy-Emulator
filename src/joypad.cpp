#include "joypad.h"
#include "bus.h"

static constexpr u16 IO_IF  = 0xFF0F;

void Joypad::write(u8 v) {
    // Only bits 5..4 are meaningful on write. 0 selects, 1 deselects.
    select_bits_ = static_cast<u8>(v & 0x30);
}

u8 Joypad::read() const {
    // Upper bits typically read as 1.
    u8 res = 0xC0;          // bits 7..6 = 1
    res |= select_bits_;    // keep written select bits

    // Lower 4: P10..P13. 1 = released, 0 = pressed (for selected group(s)).
    // If neither group selected, hardware leaves 1s; we’ll OR both groups logically.
    bool sel_dir  = ( (select_bits_ & (1<<4)) == 0 ); // P14=0 selects D-pad
    bool sel_act  = ( (select_bits_ & (1<<5)) == 0 ); // P15=0 selects A/B/Start/Select

    // Start with all released
    u8 low = 0x0F;

    if (sel_dir) {
        // P10=Right, P11=Left, P12=Up, P13=Down
        if (right_) low &= ~0x01;
        if (left_)  low &= ~0x02;
        if (up_)    low &= ~0x04;
        if (down_)  low &= ~0x08;
    }
    if (sel_act) {
        // P10=A, P11=B, P12=Select, P13=Start
        if (a_)      low &= ~0x01;
        if (b_)      low &= ~0x02;
        if (select_) low &= ~0x04;
        if (start_)  low &= ~0x08;
    }

    res |= low;
    return res;
}

void Joypad::set(Button btn, bool pressed, Bus* bus) {
    bool* t = nullptr;
    switch (btn) {
        case Button::Right:  t = &right_;  break;
        case Button::Left:   t = &left_;   break;
        case Button::Up:     t = &up_;     break;
        case Button::Down:   t = &down_;   break;
        case Button::A:      t = &a_;      break;
        case Button::B:      t = &b_;      break;
        case Button::Select: t = &select_; break;
        case Button::Start:  t = &start_;  break;
    }
    bool was = *t;
    *t = pressed;

    // If a button transitioned to pressed, request Joypad interrupt (IF bit 4).
    if (!was && pressed && bus) {
        u8 iff = bus->read8(IO_IF);
        bus->write8(IO_IF, static_cast<u8>(iff | (1 << 4)));
    }
}
