#include "RemoteCore.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
using namespace remote;
static int tests=0;
#define RUN(test) do { test(); ++tests; std::printf("PASS %s\n",#test); } while(0)
constexpr uint8_t bit(Button b) { return 1u << b; }
constexpr uint8_t AB=bit(A)|bit(B);

void phone_exact_mapping() {
    const uint16_t expected[] = {0xB6,0xB5,0xE9,0xEA,0xCD,0xE2};
    for (int i=0;i<ButtonCount;++i) {
        const auto& a=getProfile(ProfileId::PhoneMedia).actions[i];
        assert(a.kind==Consumer && a.usage==expected[i] && a.modifiers==0);
    }
}
void presentation_exact_mapping() {
    const uint16_t expected[] = {0x4B,0x4E,0x4A,0x4D,0x3E,0x29};
    for (int i=0;i<ButtonCount;++i) {
        const auto& a=getProfile(ProfileId::Presentation).actions[i];
        assert(a.kind==Keyboard && a.usage==expected[i] && a.modifiers==0 && !a.repeat);
    }
}
void computer_media_exact_mapping() {
    const uint16_t expected[] = {0xE9,0xEA,0xB6,0xB5,0xCD,0xE2};
    for (int i=0;i<ButtonCount;++i) {
        const auto& a=getProfile(ProfileId::ComputerMedia).actions[i];
        assert(a.kind==Consumer && a.usage==expected[i] && a.modifiers==0);
    }
}
void only_three_fixed_profiles() {
    static_assert(ProfileCount==3, "Only requested profiles");
    for (const auto& p:profiles) for (const auto& a:p.actions) assert(validAction(a));
    assert(!strcmp(profileName(ProfileId::PhoneMedia),"Phone Media"));
    assert(!strcmp(profileName(ProfileId::Presentation),"Presentation"));
    assert(!strcmp(profileName(ProfileId::ComputerMedia),"PC Media"));
}
void only_volume_keys_repeat() {
    assert(getRepeatMask(ProfileId::PhoneMedia)==(bit(Left)|bit(Right)));
    assert(getRepeatMask(ProfileId::ComputerMedia)==(bit(Up)|bit(Down)));
    assert(!getRepeatMask(ProfileId::Presentation));
}
void invalid_actions_rejected() {
    Action a=getProfile(ProfileId::PhoneMedia).actions[Up];
    a.usage=0xFFFF; assert(!validAction(a));
    a=getProfile(ProfileId::PhoneMedia).actions[A]; a.modifiers=1; assert(!validAction(a));
    a=getProfile(ProfileId::Presentation).actions[A]; a.modifiers=0x10; assert(!validAction(a));
    a=getProfile(ProfileId::Presentation).actions[A]; memset(a.label,'X',sizeof(a.label)); assert(!validAction(a));
}
void web_menu_has_no_ble() {
    Navigation n; n.move(-1); assert(n.selection()==2); n.select();
    assert(n.screen()==Screen::WebSetup && !n.wantsConnection());
    n.move(1); assert(n.screen()==Screen::WebSetup);
    assert(n.back() && n.screen()==Screen::ChooseDevice && n.selection()==2);
}
void boot_device_menu_only() {
    Navigation n; assert(n.screen()==Screen::ChooseDevice && !n.wantsConnection());
    assert(n.selection()==0 && !n.back());
}
void phone_selects_directly() {
    Navigation n; assert(n.select());
    assert(n.screen()==Screen::Run && n.profile()==ProfileId::PhoneMedia);
    assert(n.target()==Target::Phone && n.wantsConnection());
}
void phone_back_disconnects() {
    Navigation n; n.select(); assert(n.back());
    assert(n.screen()==Screen::ChooseDevice && !n.wantsConnection());
}
void computer_submenu_has_two_items() {
    Navigation n; n.move(1); n.select();
    assert(n.screen()==Screen::ComputerModes && n.wantsConnection());
    assert(n.selection()==0); n.move(1); assert(n.selection()==1);
    n.move(1); assert(n.selection()==0); n.move(-1); assert(n.selection()==1);
}
void presentation_selection_and_return() {
    Navigation n; n.move(1); n.select(); n.select();
    assert(n.screen()==Screen::Run && n.profile()==ProfileId::Presentation);
    assert(n.back() && n.screen()==Screen::ComputerModes && n.selection()==0);
    assert(n.wantsConnection());
}
void pc_media_selection_and_return() {
    Navigation n; n.move(1); n.select(); n.move(1); n.select();
    assert(n.screen()==Screen::Run && n.profile()==ProfileId::ComputerMedia);
    n.back(); assert(n.screen()==Screen::ComputerModes && n.selection()==1 && n.wantsConnection());
    n.back(); assert(n.screen()==Screen::ChooseDevice && !n.wantsConnection());
}
void mode_does_not_change_from_menu_direction_when_running() {
    Navigation n; n.select(); n.move(1); assert(n.profile()==ProfileId::PhoneMedia);
    assert(!n.select()); assert(n.screen()==Screen::Run);
}
void every_reset_starts_device_selection() {
    Navigation old; old.move(1); old.select(); old.move(1); old.select();
    Navigation fresh; assert(fresh.screen()==Screen::ChooseDevice && !fresh.wantsConnection());
}
void aborted_chord_waits_for_all_buttons() {
    ControlInput c; c.update(0,bit(Left),0);
    c.update(AB|bit(Left),bit(Left),10);
    assert(!c.update(bit(Left),bit(Left),200).fireMask);
    assert(!c.update(bit(Left),bit(Left),1000).fireMask);
    assert(!c.update(0,bit(Left),1100).fireMask);
    assert(c.update(bit(Left),bit(Left),1200).fireMask==bit(Left));
}
void debounce_edges() {
    Debouncer d;
    d.update(false,0,18); assert(!d.down());
    d.update(true,1,18); assert(!d.rose());
    d.update(false,5,18); d.update(true,8,18);
    d.update(true,25,18); assert(!d.rose());
    d.update(true,26,18); assert(d.rose() && d.down());
    d.update(true,1000,18); assert(!d.rose());
    d.update(false,1001,18); d.update(false,1019,18);
    assert(d.fell() && !d.down());
}
void debounce_clock_wrap() {
    Debouncer d; d.update(true,0xFFFFFFF0u,18);
    d.update(true,2,18); assert(d.rose());
}
void entry_waits_for_release() {
    ControlInput c; c.reset();
    assert(!c.update(bit(A),0,0).fireMask);
    assert(!c.update(0,0,200).fireMask);
    assert(!c.update(bit(A),0,300).fireMask);
    assert(c.update(0,0,350).fireMask == bit(A));
}
void direction_press_once() {
    ControlInput c; c.update(0,0,0);
    assert(c.update(bit(Up),0,10).fireMask == bit(Up));
    assert(!c.update(bit(Up),0,1000).fireMask);
    assert(!c.update(0,0,1100).fireMask);
}
void direction_repeat() {
    ControlInput c; c.update(0,0,0);
    assert(c.update(bit(Up),bit(Up),10).fireMask == bit(Up));
    assert(!c.update(bit(Up),bit(Up),459).fireMask);
    assert(c.update(bit(Up),bit(Up),460).fireMask == bit(Up));
    assert(!c.update(bit(Up),bit(Up),579).fireMask);
    assert(c.update(bit(Up),bit(Up),580).fireMask == bit(Up));
}
void ab_short_taps() {
    ControlInput c; c.update(0,0,0);
    assert(!c.update(bit(A),0,10).fireMask);
    assert(c.update(0,0,40).fireMask == bit(A));
    assert(!c.update(bit(B),0,50).fireMask);
    assert(c.update(0,0,80).fireMask == bit(B));
}
void chord_exits_without_actions() {
    ControlInput c; c.update(0,0,0);
    assert(!c.update(bit(A),0,10).fireMask);
    auto e=c.update(AB,0,60); assert(e.chordActive && !e.fireMask && !e.exit);
    e=c.update(AB,0,1259); assert(!e.exit && !e.fireMask);
    e=c.update(AB,0,1260); assert(e.exit && !e.fireMask);
    assert(!c.update(bit(B),0,1270).fireMask);
    assert(!c.update(0,0,1280).fireMask);
}
void aborted_chord_a_released_first() {
    ControlInput c; c.update(0,0,0);
    c.update(bit(A),0,10); c.update(AB,0,40);
    assert(!c.update(bit(B),0,100).fireMask);
    assert(!c.update(0,0,200).fireMask);
    c.update(bit(A),0,300); assert(c.update(0,0,350).fireMask==bit(A));
}
void aborted_chord_b_released_first() {
    ControlInput c; c.update(0,0,0);
    c.update(bit(B),0,10); c.update(AB,0,40);
    assert(!c.update(bit(A),0,100).fireMask);
    assert(!c.update(0,0,200).fireMask);
}
void aborted_chord_simultaneous_release() {
    ControlInput c; c.update(0,0,0); c.update(AB,0,10);
    assert(!c.update(0,0,100).fireMask);
}
void chord_clock_wrap() {
    ControlInput c; c.update(0,0,0xFFFFFE00u);
    c.update(AB,0,0xFFFFFF00u);
    auto e=c.update(AB,0,0x000003AFu); assert(!e.exit);
    e=c.update(AB,0,0x000003B0u); assert(e.exit && !e.fireMask);
}
void reset_discards_old_actions() {
    ControlInput c; c.update(0,0,0); c.update(bit(A),0,1);
    c.reset(); assert(!c.update(0,0,20).fireMask);
}
void repeat_clock_wrap() {
    ControlInput c; c.update(0,0,0xFFFFFE00u);
    c.update(bit(Left),bit(Left),0xFFFFFF00u);
    assert(c.update(bit(Left),bit(Left),0x000000C2u).fireMask == bit(Left));
}
void random_state_invariants() {
    uint32_t random=42, now=0; ControlInput c; c.update(0,0,0);
    for (int i=0;i<100000;++i) {
        random = random*1664525u+1013904223u; now += (random>>24)+1;
        uint8_t mask = (random>>16)&0x3F;
        auto e=c.update(mask,0x0F,now);
        assert(!(e.fireMask & ~0x3F));
        if ((mask&AB)==AB) assert(!(e.fireMask&AB));
        if (e.exit) assert((mask&AB)==AB && !e.fireMask);
    }
}
int main() {
    RUN(phone_exact_mapping);
    RUN(presentation_exact_mapping);
    RUN(computer_media_exact_mapping);
    RUN(only_three_fixed_profiles);
    RUN(only_volume_keys_repeat);
    RUN(invalid_actions_rejected);
    RUN(web_menu_has_no_ble);
    RUN(boot_device_menu_only);
    RUN(phone_selects_directly);
    RUN(phone_back_disconnects);
    RUN(computer_submenu_has_two_items);
    RUN(presentation_selection_and_return);
    RUN(pc_media_selection_and_return);
    RUN(mode_does_not_change_from_menu_direction_when_running);
    RUN(every_reset_starts_device_selection);
    RUN(aborted_chord_waits_for_all_buttons);
    RUN(debounce_edges);
    RUN(debounce_clock_wrap);
    RUN(entry_waits_for_release);
    RUN(direction_press_once);
    RUN(direction_repeat);
    RUN(ab_short_taps);
    RUN(chord_exits_without_actions);
    RUN(aborted_chord_a_released_first);
    RUN(aborted_chord_b_released_first);
    RUN(aborted_chord_simultaneous_release);
    RUN(chord_clock_wrap);
    RUN(reset_discards_old_actions);
    RUN(repeat_clock_wrap);
    RUN(random_state_invariants);
    std::printf("\n%d portable logic tests passed. No ESP32 hardware used.\n",tests);
}
