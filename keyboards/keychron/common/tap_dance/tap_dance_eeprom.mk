TAP_DANCE_EEPROM_DIR = common/tap_dance
SRC += \
     $(TAP_DANCE_EEPROM_DIR)/tap_dance_eeprom.c

VPATH += $(TOP_DIR)/keyboards/keychron/$(TAP_DANCE_EEPROM_DIR)

OPT_DEFS += -DDYNAMIC_TAP_DANCE_ENABLE
