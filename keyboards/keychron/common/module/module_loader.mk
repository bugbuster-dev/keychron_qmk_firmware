# Module Loader Build Integration
# Adds module source files to the build when MODULE_LOADER_ENABLE is set.

OPT_DEFS += -DMODULE_LOADER_ENABLE

MODULE_LOADER_DIR = keyboards/keychron/common/module
VPATH += $(TOP_DIR)/$(MODULE_LOADER_DIR)

SRC += keyboards/keychron/common/module/module_flash.c
SRC += keyboards/keychron/common/module/module_loader.c
SRC += keyboards/keychron/common/module/module_dispatch.c

# Enable the QMK combo callback hooks so module dispatchers are actually
# invoked by quantum/process_keycode/process_combo.c. Without these defines
# the weak callbacks are never called from core, regardless of strong
# overrides, which leaves loaded modules inert.
#
# combo_ref_from_layer is always called by core and needs no flag.
ifeq ($(strip $(COMBO_ENABLE)), yes)
OPT_DEFS += -DCOMBO_SHOULD_TRIGGER
OPT_DEFS += -DCOMBO_TERM_PER_COMBO
OPT_DEFS += -DCOMBO_MUST_HOLD_PER_COMBO
OPT_DEFS += -DCOMBO_MUST_TAP_PER_COMBO
OPT_DEFS += -DCOMBO_MUST_PRESS_IN_ORDER_PER_COMBO
OPT_DEFS += -DCOMBO_PROCESS_KEY_RELEASE
OPT_DEFS += -DCOMBO_PROCESS_KEY_REPRESS
endif
