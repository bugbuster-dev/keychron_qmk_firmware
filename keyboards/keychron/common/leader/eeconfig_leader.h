// keyboards/keychron/common/leader/eeconfig_leader.h
#pragma once

#define LEADER_DEF_MAX_SLOTS 8
#define LEADER_DEF_MAX_SEQ_LEN 5

// 1 byte magic + 8 slots x 12 bytes (5 x uint16_t sequence + 1 x uint16_t keycode) = 97 bytes
#define EECONFIG_SIZE_LEADER (1 + LEADER_DEF_MAX_SLOTS * 12)
