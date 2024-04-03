#OPT_DEFS += -DFACTORY_TEST_ENABLE

KEYCHRON_COMMON_DIR = common
SRC += \
	$(KEYCHRON_COMMON_DIR)/keychron_task.c \
    $(KEYCHRON_COMMON_DIR)/keychron_common.c \

ifneq (,$(findstring FACTORY_TEST_ENABLE,$(OPT_DEFS)))
SRC += $(KEYCHRON_COMMON_DIR)/factory_test.c
endif

VPATH += $(TOP_DIR)/keyboards/keychron/$(KEYCHRON_COMMON_DIR)
