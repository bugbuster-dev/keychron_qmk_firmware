// keyboards/keychron/common/leader/leader_eeprom.c
#include <string.h>
#include "leader_eeprom.h"
#include "eeconfig_kb.h"
#include "eeconfig.h"
#include "eeprom.h"
#include "quantum.h"

#if defined(DYNAMIC_LEADER_ENABLE) && defined(LEADER_ENABLE)

#    define LDR_EEPROM_MAGIC 0xAE
#    define LDR_EEPROM_MAGIC_ADDR ((uint8_t *)EECONFIG_BASE_LEADER)
#    define LDR_EEPROM_DATA_ADDR ((uint8_t *)EECONFIG_BASE_LEADER + 1)

// RAM mirror
static leader_def_t leader_defs[LEADER_DEF_MAX_SLOTS];

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void leader_eeprom_save(void) {
    eeprom_update_byte(LDR_EEPROM_MAGIC_ADDR, LDR_EEPROM_MAGIC);
    eeprom_update_block(leader_defs, LDR_EEPROM_DATA_ADDR, sizeof(leader_defs));
}

void leader_eeprom_init(void) {
    uint8_t magic = eeprom_read_byte(LDR_EEPROM_MAGIC_ADDR);
    if (magic == LDR_EEPROM_MAGIC) {
        eeprom_read_block(leader_defs, LDR_EEPROM_DATA_ADDR, sizeof(leader_defs));
    } else {
        leader_eeprom_reset_defaults();
    }
}

void leader_eeprom_set(uint8_t slot, const leader_def_t *def) {
    if (slot >= LEADER_DEF_MAX_SLOTS || !def) return;
    leader_defs[slot] = *def;
    leader_eeprom_save();
}

void leader_eeprom_get(uint8_t slot, leader_def_t *out) {
    if (slot >= LEADER_DEF_MAX_SLOTS || !out) return;
    *out = leader_defs[slot];
}

void leader_eeprom_clear(uint8_t slot) {
    if (slot >= LEADER_DEF_MAX_SLOTS) return;
    memset(&leader_defs[slot], 0, sizeof(leader_def_t));
    leader_eeprom_save();
}

void leader_eeprom_reset_defaults(void) {
    memset(leader_defs, 0, sizeof(leader_defs));
    leader_eeprom_save();
}

// ---------------------------------------------------------------------------
// Matching engine
// ---------------------------------------------------------------------------

bool leader_eeprom_try_match(const uint16_t *sequence, uint8_t seq_len) {
    if (seq_len == 0) return false;

    for (uint8_t i = 0; i < LEADER_DEF_MAX_SLOTS; i++) {
        if (leader_defs[i].sequence[0] == KC_NO) continue; // slot disabled
        if (leader_defs[i].keycode == KC_NO) continue;     // no action

        // Check if current input matches this slot's sequence exactly
        bool match = true;
        for (uint8_t k = 0; k < seq_len; k++) {
            if (k >= LEADER_DEF_MAX_SEQ_LEN || leader_defs[i].sequence[k] != sequence[k]) {
                match = false;
                break;
            }
        }
        if (!match) continue;

        // Exact match: slot sequence ends right after our input
        // (next position is KC_NO or we've used all 5 slots)
        if (seq_len >= LEADER_DEF_MAX_SEQ_LEN || leader_defs[i].sequence[seq_len] == KC_NO) {
            tap_code16(leader_defs[i].keycode);
            return true;
        }
    }

    return false;
}

bool leader_eeprom_has_prefix(const uint16_t *sequence, uint8_t seq_len) {
    if (seq_len == 0) return true; // empty sequence is prefix of everything

    for (uint8_t i = 0; i < LEADER_DEF_MAX_SLOTS; i++) {
        if (leader_defs[i].sequence[0] == KC_NO) continue;
        if (leader_defs[i].keycode == KC_NO) continue;

        bool prefix_ok = true;
        for (uint8_t k = 0; k < seq_len; k++) {
            if (k >= LEADER_DEF_MAX_SEQ_LEN || leader_defs[i].sequence[k] != sequence[k]) {
                prefix_ok = false;
                break;
            }
        }
        if (!prefix_ok) continue;

        // This slot's sequence starts with our input -- it's a valid prefix
        // (could be an exact match too, but that's handled by try_match first)
        return true;
    }

    return false;
}

#endif // DYNAMIC_LEADER_ENABLE && LEADER_ENABLE
