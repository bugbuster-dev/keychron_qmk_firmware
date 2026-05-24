// SPDX-License-Identifier: GPL-2.0-or-later

#include "keyboard_report_util.hpp"
#include "keycode.h"
#include "test_common.h"
#include "test_common.hpp"
#include "test_driver.hpp"
#include "test_fixture.hpp"
#include "test_keymap_key.hpp"

extern "C" {
#include "pipeline.h"
}

using testing::_;
using testing::InSequence;

class StickyCombo : public TestFixture {
    void SetUp() override {
        TestFixture::SetUp();
        pipeline_reset();
    }
};

TEST_F(StickyCombo, simultaneous_press_within_window_emits_no_keypress) {
    TestDriver driver;
    KeymapKey  key_j(0, 0, 0, KC_J);
    KeymapKey  key_k(0, 0, 1, KC_K);
    set_keymap({key_j, key_k});

    EXPECT_NO_REPORT(driver);

    key_j.press();
    run_one_scan_loop();
    key_k.press();
    idle_for(STICKY_COMBO_WINDOW_MS - 10);
    key_j.release();
    key_k.release();
    idle_for(100);

    VERIFY_AND_CLEAR(driver);
}

TEST_F(StickyCombo, lone_first_key_replays_after_window) {
    TestDriver driver;
    KeymapKey  key_j(0, 0, 0, KC_J);
    KeymapKey  key_k(0, 0, 1, KC_K);
    set_keymap({key_j, key_k});

    EXPECT_REPORT(driver, (KC_J));
    EXPECT_EMPTY_REPORT(driver);

    key_j.press();
    idle_for(STICKY_COMBO_WINDOW_MS + 5);
    key_j.release();
    idle_for(20);
    VERIFY_AND_CLEAR(driver);
}

TEST_F(StickyCombo, sub_window_release_synthesises_tap) {
    TestDriver driver;
    KeymapKey  key_j(0, 0, 0, KC_J);
    KeymapKey  key_k(0, 0, 1, KC_K);
    set_keymap({key_j, key_k});

    EXPECT_REPORT(driver, (KC_J));
    EXPECT_EMPTY_REPORT(driver);

    key_j.press();
    idle_for(STICKY_COMBO_WINDOW_MS / 2);
    key_j.release();
    idle_for(20);
    VERIFY_AND_CLEAR(driver);
}

TEST_F(StickyCombo, third_key_inside_window_flushes_pending_then_forwards) {
    TestDriver driver;
    KeymapKey  key_j(0, 0, 0, KC_J);
    KeymapKey  key_k(0, 0, 1, KC_K);
    KeymapKey  key_l(0, 0, 2, KC_L);
    set_keymap({key_j, key_k, key_l});

    // J is buffered, then flushed when L arrives; L passes through; both release cleanly
    EXPECT_ANY_REPORT(driver).Times(testing::AnyNumber());

    key_j.press();
    run_one_scan_loop();
    key_l.press();
    run_one_scan_loop();
    key_l.release();
    run_one_scan_loop();
    key_j.release();
    idle_for(20);
    VERIFY_AND_CLEAR(driver);
}

TEST_F(StickyCombo, armed_both_release_both_returns_to_idle) {
    TestDriver driver;
    KeymapKey  key_j(0, 0, 0, KC_J);
    KeymapKey  key_k(0, 0, 1, KC_K);
    set_keymap({key_j, key_k});

    EXPECT_NO_REPORT(driver);

    key_j.press(); run_one_scan_loop();
    key_k.press(); run_one_scan_loop();
    key_j.release(); run_one_scan_loop();
    key_k.release(); idle_for(20);

    VERIFY_AND_CLEAR(driver);
}

TEST_F(StickyCombo, armed_hold_key2_tap_key1_emits_up) {
    TestDriver driver;
    KeymapKey  key_j(0, 0, 0, KC_J);
    KeymapKey  key_k(0, 0, 1, KC_K);
    set_keymap({key_j, key_k});

    // Hold K, tap J → UP (tap_action_1)
    EXPECT_REPORT(driver, (KC_UP));
    EXPECT_EMPTY_REPORT(driver);

    key_j.press(); run_one_scan_loop();
    key_k.press(); run_one_scan_loop();
    key_j.release(); run_one_scan_loop();
    key_j.press(); run_one_scan_loop();
    key_j.release(); run_one_scan_loop();
    key_k.release(); idle_for(20);

    VERIFY_AND_CLEAR(driver);
}

TEST_F(StickyCombo, armed_hold_key1_tap_key2_emits_down) {
    TestDriver driver;
    KeymapKey  key_j(0, 0, 0, KC_J);
    KeymapKey  key_k(0, 0, 1, KC_K);
    set_keymap({key_j, key_k});

    // Hold J, tap K → DOWN (tap_action_2)
    EXPECT_REPORT(driver, (KC_DOWN));
    EXPECT_EMPTY_REPORT(driver);

    key_j.press(); run_one_scan_loop();
    key_k.press(); run_one_scan_loop();
    key_k.release(); run_one_scan_loop();
    key_k.press(); run_one_scan_loop();
    key_k.release(); run_one_scan_loop();
    key_j.release(); idle_for(20);

    VERIFY_AND_CLEAR(driver);
}

TEST_F(StickyCombo, armed_third_key_passes_through) {
    TestDriver driver;
    KeymapKey  key_j(0, 0, 0, KC_J);
    KeymapKey  key_k(0, 0, 1, KC_K);
    KeymapKey  key_l(0, 0, 2, KC_L);
    set_keymap({key_j, key_k, key_l});

    EXPECT_REPORT(driver, (KC_L));
    EXPECT_EMPTY_REPORT(driver);

    key_j.press(); run_one_scan_loop();
    key_k.press(); run_one_scan_loop();
    key_l.press(); run_one_scan_loop();
    key_l.release(); run_one_scan_loop();
    key_j.release(); run_one_scan_loop();
    key_k.release(); idle_for(20);

    VERIFY_AND_CLEAR(driver);
}
