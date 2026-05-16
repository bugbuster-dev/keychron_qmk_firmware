# Module Loader Build Integration
# Adds module source files to the build when MODULE_LOADER_ENABLE is set.

OPT_DEFS += -DMODULE_LOADER_ENABLE

MODULE_LOADER_DIR = keyboards/keychron/common/module
VPATH += $(TOP_DIR)/$(MODULE_LOADER_DIR)

SRC += keyboards/keychron/common/module/module_flash.c
SRC += keyboards/keychron/common/module/module_loader.c
SRC += keyboards/keychron/common/module/module_dispatch.c
SRC += keyboards/keychron/common/module/module_log.c

# SRAM module support (volatile, no flash wear). Opt-in per keymap.
# If MODULE_SRAM_TOTAL_SIZE doesn't fit, the link fails with
# "region 'ram0' overflowed" — by design.
ifeq ($(strip $(MODULE_SRAM_ENABLE)), yes)
OPT_DEFS += -DMODULE_SRAM_ENABLE
SRC += keyboards/keychron/common/module/module_sram.c
endif

# Force the linker to keep mprintf even though no firmware-side code
# calls it. Modules resolve it via host-side .map-based PROVIDE symbol
# resolution (qmk-tools QMKata ModuleBuild.py), which requires the
# symbol to survive --gc-sections. __attribute__((used)) alone is not
# enough because it only prevents compiler-level discard; the linker
# still garbage-collects unreferenced sections.
EXTRALDFLAGS += -Wl,--undefined=mprintf

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
