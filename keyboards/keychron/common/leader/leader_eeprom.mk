LEADER_EEPROM_DIR = common/leader
SRC += \
     $(LEADER_EEPROM_DIR)/leader_eeprom.c

VPATH += $(TOP_DIR)/keyboards/keychron/$(LEADER_EEPROM_DIR)

OPT_DEFS += -DDYNAMIC_LEADER_ENABLE
