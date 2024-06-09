include keyboards/keychron/common/wireless/wireless.mk
include keyboards/keychron/common/keychron_common.mk

include keyboards/keychron/qmkata/qmkata.mk

SRC += \
q3_max_user.c \
debug_user.c \

VPATH += $(TOP_DIR)/keyboards/keychron
