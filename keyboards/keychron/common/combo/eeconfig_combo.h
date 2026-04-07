#pragma once

#define COMBO_DEF_MAX_KEYS 8
#define COMBO_DEF_MAX_SLOTS 16

// 1 byte magic + 16 * 18 bytes = 289 bytes
#define EECONFIG_SIZE_COMBO (1 + COMBO_DEF_MAX_SLOTS * (COMBO_DEF_MAX_KEYS * 2 + 2))
