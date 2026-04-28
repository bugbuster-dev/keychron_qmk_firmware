#pragma once

#define COMBO_DEF_MAX_KEYS 8
#define COMBO_DEF_MAX_SLOTS 10

// 1 byte magic + 10 * 18 bytes = 181 bytes
#define EECONFIG_SIZE_COMBO (1 + COMBO_DEF_MAX_SLOTS * (COMBO_DEF_MAX_KEYS * 2 + 2))
