OPT_DEFS += -DFIRMATA_ENABLE

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
CONSOLE_VIRTSER = yes
VIRTSER_ENABLE = yes
RGB_MATRIX_CUSTOM_USER = yes
