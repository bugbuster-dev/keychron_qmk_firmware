# State machine key processing pipeline
ifdef KEY_PROCESSING_SM_ENABLE
    SRC += quantum/pipeline.c
    VPATH += $(QUANTUM_DIR)/features
    EXTRAINCDIRS += $(QUANTUM_DIR)/features

    ifdef VIM_MODAL_ENABLE
        SRC += quantum/features/vim_modal_adapter.c
        SRC += quantum/features/VimModal.c
        OPT_DEFS += -DVIM_MODAL_ENABLE
    endif

    ifdef STICKY_COMBO_ENABLE
        SRC += quantum/features/sticky_combo_adapter.c
        SRC += quantum/features/StickyCombo.c
        OPT_DEFS += -DSTICKY_COMBO_ENABLE
    endif
endif

# StateSmith code generation (PlantUML → C)
STATESMITH ?= ~/.local/bin/statesmith
PUMLS := $(wildcard quantum/features/*.puml)
GENERATED_C := $(patsubst quantum/features/%.puml,quantum/features/%.c,$(PUMLS))

# Generate C from .puml files (only when .puml is newer than generated .c)
# StateSmith outputs <Diagram>.c where Diagram is taken from @startuml line
.PHONY: statesmith-gen
statesmith-gen:
	@for f in $(PUMLS); do $(STATESMITH) run --lang C99 --no-csx --no-ask $$f; done

# SRAM module support (volatile, no flash wear). Opt-in per keymap.
# Lives here (not in module_loader.mk) because keymap-level rules.mk
# variables are not yet visible when keychron_common.mk is processed.
# If MODULE_SRAM_TOTAL_SIZE doesn't fit, the link fails with
# "region 'ram0' overflowed" — by design.
ifeq ($(strip $(MODULE_SRAM_ENABLE)), yes)
    OPT_DEFS += -DMODULE_SRAM_ENABLE
    SRC += keyboards/keychron/common/module/module_sram.c
endif
