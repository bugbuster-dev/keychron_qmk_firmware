OPT_DEFS += -DQMKATA_ENABLE -DRAW_EPSIZE_QMKATA=64 -DDIP_SWITCH_STATE_STATIC=
#OPT_DEFS += -DDEVEL_BUILD

QMKATA_DIR = qmkata

SRC += \
qmkata_sysex_handler.c \
qmkata_rgb_matrix_user.c \
$(QMKATA_DIR)/FirmataParser.cpp \
$(QMKATA_DIR)/FirmataMarshaller.cpp \
$(QMKATA_DIR)/Firmata.cpp \
$(QMKATA_DIR)/QMKata.cpp \
$(QMKATA_DIR)/Print.cpp \
#empty line

CONSOLE_ENABLE = yes
CONSOLE_QMKATA = yes
RGB_MATRIX_CUSTOM_USER = yes
