OPT_DEFS += -DFIRMATA_ENABLE -DRAW_EPSIZE_FIRMATA=64 -DDIP_SWITCH_STATE_STATIC=
#OPT_DEFS += -DDEVEL_BUILD

FIRMATA_DIR = firmata

SRC += \
qmkata_sysex_handler.c \
qmkata_rgb_matrix_user.c \
$(FIRMATA_DIR)/FirmataParser.cpp \
$(FIRMATA_DIR)/FirmataMarshaller.cpp \
$(FIRMATA_DIR)/Firmata.cpp \
$(FIRMATA_DIR)/QMKata.cpp \
$(FIRMATA_DIR)/Print.cpp \
#empty line

CONSOLE_ENABLE = yes
CONSOLE_FIRMATA = yes
RGB_MATRIX_CUSTOM_USER = yes
