#pragma once
#include "types.h"

class Bus;

enum class Button {
    Right, Left, Up, Down, A, B, Select, Start
};

class Joypad {
public:
    Joypad() = default;

    //Bus hooks
    void write(u8 v);
    u8 read() const;

    void set(Button btn, bool pressed, Bus* bus_if_available = nullptr);

private:
    u8 select_bits_ { 0x30 }; // default: both groups unselected
    // Button states: true = pressed, false = released
    bool right_ { false }, left_ { false },
         up_ { false }, down_ { false },
         a_ { false }, b_ { false },
         select_ { false }, start_ { false };
};