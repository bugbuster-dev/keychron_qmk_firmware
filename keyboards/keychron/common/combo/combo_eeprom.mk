COMBO_EEPROM_DIR = common/combo
SRC += \
     $(COMBO_EEPROM_DIR)/combo_eeprom.c

VPATH += $(TOP_DIR)/keyboards/keychron/$(COMBO_EEPROM_DIR)

OPT_DEFS += -DDYNAMIC_COMBO_ENABLE
