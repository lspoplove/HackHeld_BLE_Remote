#include "RemoteCore.h"
#include <string.h>

namespace remote {
const char* const profileNames[ProfileCount] = {"Phone Media", "Presentation", "PC Media"};
const char* const shortButtonNames[ButtonCount] = {"U", "D", "L", "R", "A", "B"};

// Factory default mappings, in physical Up, Down, Left, Right, A, B order.
// HID Consumer usages: previous B6, next B5, volume+ E9, volume- EA,
// play/pause CD, mute E2. They are not ASCII keyboard characters.
const Profile profiles[ProfileCount] = {
    // PHONE: Up/Down change tracks; Left/Right change volume.
    {{ {0x00B6, Consumer, 0, 0, "Prev track"},
       {0x00B5, Consumer, 0, 0, "Next track"},
       {0x00E9, Consumer, 0, 1, "Volume +"},
       {0x00EA, Consumer, 0, 1, "Volume -"},
       {0x00CD, Consumer, 0, 0, "Play/Pause"},
       {0x00E2, Consumer, 0, 0, "Mute"} }},
    // COMPUTER PRESENTATION: exactly the v1 default keyboard usages.
    {{ {0x004B, Keyboard, 0, 0, "Prev slide"},
       {0x004E, Keyboard, 0, 0, "Next slide"},
       {0x004A, Keyboard, 0, 0, "First slide"},
       {0x004D, Keyboard, 0, 0, "Last slide"},
       {0x003E, Keyboard, 0, 0, "Start (F5)"},
       {0x0029, Keyboard, 0, 0, "Exit (Esc)"} }},
    // COMPUTER MEDIA: Up/Down change volume; Left/Right change tracks.
    {{ {0x00E9, Consumer, 0, 1, "Volume +"},
       {0x00EA, Consumer, 0, 1, "Volume -"},
       {0x00B6, Consumer, 0, 0, "Prev track"},
       {0x00B5, Consumer, 0, 0, "Next track"},
       {0x00CD, Consumer, 0, 0, "Play/Pause"},
       {0x00E2, Consumer, 0, 0, "Mute"} }}
};

namespace {
RemoteConfig activeConfig{};
bool activeLoaded = false;
void ensureConfig() { if (!activeLoaded) { defaultConfig(activeConfig); activeLoaded = true; } }
}
void defaultConfig(RemoteConfig& out) { memcpy(out.profiles, profiles, sizeof(profiles)); }
void currentConfig(RemoteConfig& out) { ensureConfig(); out = activeConfig; }
const Profile& getDefaultProfile(ProfileId id) {
    const auto index = static_cast<uint8_t>(id);
    return profiles[index < ProfileCount ? index : 0];
}
const Profile& getProfile(ProfileId id) {
    ensureConfig();
    const auto index = static_cast<uint8_t>(id);
    return activeConfig.profiles[index < ProfileCount ? index : 0];
}
const char* profileName(ProfileId id) {
    const auto index = static_cast<uint8_t>(id);
    return profileNames[index < ProfileCount ? index : 0];
}
uint8_t getRepeatMask(ProfileId id) {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < 4; ++i)
        if (getProfile(id).actions[i].repeat) mask |= 1u << i;
    return mask;
}
bool validAction(const Action& a) {
    if (a.kind > Consumer || a.modifiers > 0x0F || a.repeat > 1 || !a.label[0]) return false;
    bool terminated = false;
    for (char c : a.label) {
        if (!c) { terminated = true; break; }
        if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) return false;
    }
    if (!terminated) return false;
    if (a.kind == Disabled) return !a.usage && !a.repeat && !a.modifiers;
    if (a.kind == Keyboard) {
        for (size_t i = 0; i < keyboardChoiceCount; ++i)
            if (keyboardChoices[i].usage == a.usage) return true;
        return false;
    }
    if (a.modifiers) return false;
    for (size_t i = 0; i < mediaChoiceCount; ++i)
        if (mediaChoices[i].usage == a.usage) return true;
    return false;
}
bool validConfig(const RemoteConfig& c) {
    for (const auto& p : c.profiles)
        for (uint8_t i = 0; i < ButtonCount; ++i)
            if (!validAction(p.actions[i]) || (i >= A && p.actions[i].repeat)) return false;
    return true;
}
bool applyConfig(const RemoteConfig& c) {
    if (!validConfig(c)) return false;
    activeConfig = c; activeLoaded = true; return true;
}
bool elapsed(uint32_t now, uint32_t since, uint32_t interval) {
    return static_cast<uint32_t>(now - since) >= interval;
}
void Navigation::move(int delta) {
    if (screen_ != Screen::ChooseDevice && screen_ != Screen::ComputerModes) return;
    const int count = screen_ == Screen::ChooseDevice ? 3 : 2;
    selection_ = static_cast<uint8_t>((selection_ + delta % count + count) % count);
}
bool Navigation::select() {
    if (screen_ == Screen::ChooseDevice) {
        if (selection_ == 2) { screen_ = Screen::WebSetup; return true; }
        target_ = selection_ == 0 ? Target::Phone : Target::Computer;
        selection_ = 0;
        if (target_ == Target::Phone) {
            profile_ = ProfileId::PhoneMedia;
            screen_ = Screen::Run;
        } else screen_ = Screen::ComputerModes;
        return true;
    }
    if (screen_ == Screen::ComputerModes) {
        profile_ = selection_ == 0 ? ProfileId::Presentation : ProfileId::ComputerMedia;
        screen_ = Screen::Run;
        return true;
    }
    return false;
}
bool Navigation::back() {
    if (screen_ == Screen::ChooseDevice) return false;
    if (screen_ == Screen::WebSetup) { screen_ = Screen::ChooseDevice; selection_ = 2; return true; }
    if (screen_ == Screen::Run && target_ == Target::Computer) {
        selection_ = profile_ == ProfileId::Presentation ? 0 : 1;
        screen_ = Screen::ComputerModes;
    } else {
        selection_ = target_ == Target::Phone ? 0 : 1;
        screen_ = Screen::ChooseDevice;
    }
    return true;
}
void Debouncer::update(bool rawPressed, uint32_t now, uint32_t debounceMs) {
    rose_ = fell_ = false;
    if (rawPressed != raw_) { raw_ = rawPressed; changed_ = now; }
    if (stable_ != raw_ && elapsed(now, changed_, debounceMs)) {
        stable_ = raw_; rose_ = stable_; fell_ = !stable_;
    }
}
void ControlInput::reset() {
    waitRelease_ = true; suppressAB_ = chord_ = false; previous_ = 0;
    memset(repeating_, 0, sizeof(repeating_));
}
ControlEvents ControlInput::update(uint8_t pressed, uint8_t repeatMask, uint32_t now) {
    ControlEvents e;
    pressed &= 0x3F;
    if (waitRelease_) {
        previous_ = pressed;
        if (!pressed) { waitRelease_ = false; previous_ = 0; }
        return e;
    }
    const uint8_t rising = pressed & ~previous_;
    const uint8_t falling = previous_ & ~pressed;
    const uint8_t abMask = (1u << A) | (1u << B);
    if ((pressed & abMask) == abMask) {
        suppressAB_ = true;
        if (!chord_) { chord_ = true; chordAt_ = now; }
        uint32_t duration = now - chordAt_;
        e.chordActive = true;
        e.chordMs = static_cast<uint16_t>(duration > EXIT_HOLD_MS ? EXIT_HOLD_MS : duration);
        if (elapsed(now, chordAt_, EXIT_HOLD_MS)) { e.exit = true; waitRelease_ = true; }
        previous_ = pressed;
        return e;
    }
    chord_ = false;
    if (suppressAB_) {
        // Abort any overlapping gesture cleanly; no A/B or stale direction event.
        if (!pressed) suppressAB_ = false; // wait for ALL keys, not only A/B
        previous_ = pressed;
        return e;
    }
    e.fireMask |= falling & abMask;
    for (uint8_t i = 0; i < 4; ++i) {
        const uint8_t bit = 1u << i;
        if (rising & bit) {
            e.fireMask |= bit; repeatAt_[i] = now; repeating_[i] = false;
        } else if ((pressed & repeatMask & bit)
                   && elapsed(now, repeatAt_[i], repeating_[i] ? REPEAT_INTERVAL_MS : REPEAT_DELAY_MS)) {
            e.fireMask |= bit; repeating_[i] = true; repeatAt_[i] = now;
        }
    }
    previous_ = pressed;
    return e;
}
}
