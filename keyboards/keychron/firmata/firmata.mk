OPT_DEFS += -DFIRMATA_ENABLE -DRAW_EPSIZE_FIRMATA=64
#OPT_DEFS += -DDEVEL_BUILD

FIRMATA_DIR = firmata

SRC += \
firmata_sysex_handler.c \
firmata_rgb_matrix_user.c \
$(FIRMATA_DIR)/FirmataParser.cpp \
$(FIRMATA_DIR)/FirmataMarshaller.cpp \
$(FIRMATA_DIR)/Firmata.cpp \
$(FIRMATA_DIR)/Firmata_QMK.cpp \
$(FIRMATA_DIR)/Print.cpp \
#empty line

CONSOLE_ENABLE = yes
CONSOLE_FIRMATA = yes
RGB_MATRIX_CUSTOM_USER = yes
