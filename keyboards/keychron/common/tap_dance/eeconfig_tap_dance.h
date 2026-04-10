// keyboards/keychron/common/tap_dance/eeconfig_tap_dance.h
#pragma once

#define TAP_DANCE_DEF_MAX_SLOTS  8

// 1 byte magic + 8 slots × 8 bytes (4 × uint16_t) = 65 bytes
#define EECONFIG_SIZE_TAP_DANCE  (1 + TAP_DANCE_DEF_MAX_SLOTS * 8)
