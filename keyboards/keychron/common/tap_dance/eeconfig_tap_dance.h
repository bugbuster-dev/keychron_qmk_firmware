// keyboards/keychron/common/tap_dance/eeconfig_tap_dance.h
#pragma once

#define TAP_DANCE_DEF_MAX_SLOTS  10

// 1 byte magic + 10 slots × 8 bytes (4 × uint16_t) = 81 bytes
#define EECONFIG_SIZE_TAP_DANCE  (1 + TAP_DANCE_DEF_MAX_SLOTS * 8)
