#pragma once
#include <stddef.h>
#include <stdint.h>

namespace remote {
enum Button : uint8_t { Up = 0, Down, Left, Right, A, B, ButtonCount };
enum Kind : uint8_t { Disabled = 0, Keyboard = 1, Consumer = 2 };
enum class ProfileId : uint8_t { PhoneMedia = 0, Presentation, ComputerMedia };
constexpr uint8_t ProfileCount = 3;
constexpr uint32_t EXIT_HOLD_MS = 1200;
constexpr uint32_t REPEAT_DELAY_MS = 450;
constexpr uint32_t REPEAT_INTERVAL_MS = 120;

struct Action {
    uint16_t usage;
    uint8_t kind;
    uint8_t modifiers;
    uint8_t repeat;
    char label[13]; // <= 12 ASCII characters plus NUL
};
struct Profile { Action actions[ButtonCount]; };
extern const Profile profiles[ProfileCount]; // factory defaults; never modified
struct RemoteConfig { Profile profiles[ProfileCount]; };
struct KeyChoice { uint16_t usage; const char* name; };
extern const KeyChoice keyboardChoices[];
extern const size_t keyboardChoiceCount;
extern const KeyChoice mediaChoices[];
extern const size_t mediaChoiceCount;
void defaultConfig(RemoteConfig& out);
void currentConfig(RemoteConfig& out);
bool validConfig(const RemoteConfig& config);
bool applyConfig(const RemoteConfig& config);
const Profile& getDefaultProfile(ProfileId id);
extern const char* const profileNames[ProfileCount];
extern const char* const shortButtonNames[ButtonCount];
const Profile& getProfile(ProfileId id);
const char* profileName(ProfileId id);
uint8_t getRepeatMask(ProfileId id);
bool validAction(const Action& action);
bool elapsed(uint32_t now, uint32_t since, uint32_t interval);

// Application navigation is hardware-independent and is covered by tests.
// No stored mode: every reset starts at ChooseDevice.
enum class Screen : uint8_t { ChooseDevice, ComputerModes, Run, WebSetup };
enum class Target : uint8_t { Phone = 0, Computer = 1 };
class Navigation {
public:
    Screen screen() const { return screen_; }
    uint8_t selection() const { return selection_; }
    Target target() const { return target_; }
    ProfileId profile() const { return profile_; }
    bool wantsConnection() const { return screen_ == Screen::ComputerModes || screen_ == Screen::Run; }
    void move(int delta); // root: 3 items; Computer: 2 items
    bool select();       // A in menus; returns true when screen changes
    bool back();         // B in PC menu, or A+B hold in Run
private:
    Screen screen_ = Screen::ChooseDevice;
    Target target_ = Target::Phone;
    ProfileId profile_ = ProfileId::PhoneMedia;
    uint8_t selection_ = 0;
};

class Debouncer {
public:
    void update(bool rawPressed, uint32_t now, uint32_t debounceMs);
    bool down() const { return stable_; }
    bool rose() const { return rose_; }
    bool fell() const { return fell_; }
private:
    bool raw_ = false, stable_ = false, rose_ = false, fell_ = false;
    uint32_t changed_ = 0;
};
struct ControlEvents {
    uint8_t fireMask = 0;
    bool exit = false;
    bool chordActive = false;
    uint16_t chordMs = 0;
};
// Direction keys fire on press; A/B fire on release so A+B never executes them.
class ControlInput {
public:
    void reset(); // wait until ALL buttons are released
    ControlEvents update(uint8_t pressedMask, uint8_t repeatMask, uint32_t now);
private:
    bool waitRelease_ = true, suppressAB_ = false, chord_ = false;
    uint8_t previous_ = 0;
    uint32_t chordAt_ = 0;
    uint32_t repeatAt_[4] = {};
    bool repeating_[4] = {};
};
}
