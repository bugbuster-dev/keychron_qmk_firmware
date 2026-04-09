#include <string.h>
#include "combo_eeprom.h"
#include "eeconfig_kb.h"
#include "eeconfig.h"
#include "eeprom.h"
#include "process_combo.h"
#include "action.h"

#if defined(DYNAMIC_COMBO_ENABLE) && defined(COMBO_ENABLE)

#    define COMBO_EEPROM_MAGIC 0xCB
#    define COMBO_EEPROM_MAGIC_ADDR ((uint8_t *)EECONFIG_BASE_COMBO)
#    define COMBO_EEPROM_DATA_ADDR ((uint8_t *)EECONFIG_BASE_COMBO + 1)

// EEPROM-format storage
static combo_def_t combo_defs[COMBO_DEF_MAX_SLOTS];

// RAM mirror for key arrays (combo_t.keys needs a persistent pointer)
static uint16_t ram_keys[COMBO_DEF_MAX_SLOTS][COMBO_DEF_MAX_KEYS];

// Provided by keymap.c
extern combo_t           key_combos[];
extern const combo_def_t combo_default_defs[];
extern const uint8_t     combo_default_count;

// Populate QMK's key_combos[] from combo_defs[]
static void combo_eeprom_apply(void) {
    for (uint8_t i = 0; i < COMBO_DEF_MAX_SLOTS; i++) {
        memcpy(ram_keys[i], combo_defs[i].keys, sizeof(ram_keys[i]));
        key_combos[i].keys     = ram_keys[i];
        key_combos[i].keycode  = combo_defs[i].keycode;
        key_combos[i].disabled = (ram_keys[i][0] == COMBO_END);
        key_combos[i].active   = false;
        key_combos[i].state    = 0;
    }
}

void combo_eeprom_init(void) {
    uint8_t magic = eeprom_read_byte(COMBO_EEPROM_MAGIC_ADDR);
    if (magic == COMBO_EEPROM_MAGIC) {
        eeprom_read_block(combo_defs, COMBO_EEPROM_DATA_ADDR, sizeof(combo_defs));
    } else {
        combo_eeprom_reset_defaults();
        return; // reset_defaults calls apply
    }
    combo_eeprom_apply();
}

void combo_eeprom_save(void) {
    eeprom_update_byte(COMBO_EEPROM_MAGIC_ADDR, COMBO_EEPROM_MAGIC);
    eeprom_update_block(combo_defs, COMBO_EEPROM_DATA_ADDR, sizeof(combo_defs));
}

void combo_eeprom_set(uint8_t slot, const uint16_t *keys, uint16_t keycode) {
    if (slot >= COMBO_DEF_MAX_SLOTS) return;
    memset(&combo_defs[slot], 0, sizeof(combo_def_t));
    for (uint8_t i = 0; i < COMBO_DEF_MAX_KEYS; i++) {
        combo_defs[slot].keys[i] = keys[i];
        if (keys[i] == COMBO_END) break;
    }
    combo_defs[slot].keycode = keycode;
    combo_eeprom_apply();
    combo_eeprom_save();
}

void combo_eeprom_get(uint8_t slot, combo_def_t *out) {
    if (slot >= COMBO_DEF_MAX_SLOTS || !out) return;
    *out = combo_defs[slot];
}

void combo_eeprom_clear(uint8_t slot) {
    if (slot >= COMBO_DEF_MAX_SLOTS) return;
    memset(&combo_defs[slot], 0, sizeof(combo_def_t));
    combo_eeprom_apply();
    combo_eeprom_save();
}

void combo_eeprom_reset_defaults(void) {
    memset(combo_defs, 0, sizeof(combo_defs));
    uint8_t count = combo_default_count;
    if (count > COMBO_DEF_MAX_SLOTS) count = COMBO_DEF_MAX_SLOTS;
    memcpy(combo_defs, combo_default_defs, count * sizeof(combo_def_t));
    combo_eeprom_apply();
    combo_eeprom_save();
}

#endif // DYNAMIC_COMBO_ENABLE && COMBO_ENABLE
