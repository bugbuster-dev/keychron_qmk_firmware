# Key behavior state machine (kbsm)
ifdef KEY_BEHAVIOR_SM_ENABLE
    OPT_DEFS += -DKEY_BEHAVIOR_SM_ENABLE
    SRC += quantum/kbsm.c
    SRC += keyboards/keychron/common/module/kbsm_env.c
    VPATH += $(QUANTUM_DIR)/features
    EXTRAINCDIRS += $(QUANTUM_DIR)/features
    VPATH += $(TOP_DIR)/keyboards/keychron/common/module
    EXTRAINCDIRS += $(TOP_DIR)/keyboards/keychron/common/module

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
# StateSmith outputs <Diagram>.c where Diagram is taken from @startuml line.
# Auto-append GCC pragma guard to generated .c (StateSmith emits unused-function
# warnings that are -Werror in QMK strict builds). Linux-only (uses GNU sed/grep-perl).
.PHONY: statesmith-gen
statesmith-gen:
	@for f in $(PUMLS); do \
	    $(STATESMITH) run --lang C99 --no-csx --no-ask $$f || exit 1; \
	    cls=$$(grep -oP '^@startuml\s+\K\w+' $$f); \
	    gen=quantum/features/$$cls.c; \
	    if [ -f $$gen ] && ! grep -q "pragma GCC diagnostic push" $$gen; then \
	        sed -i '1s/^/#ifdef __GNUC__\n#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wunused-function"\n#endif\n/' $$gen; \
	        printf '\n#ifdef __GNUC__\n#pragma GCC diagnostic pop\n#endif\n' >> $$gen; \
	    fi; \
	done

# SRAM module support (volatile, no flash wear). Opt-in per keymap.
# Lives here (not in module_loader.mk) because keymap-level rules.mk
# variables are not yet visible when keychron_common.mk is processed.
# If MODULE_SRAM_TOTAL_SIZE doesn't fit, the link fails with
# "region 'ram0' overflowed" — by design.
ifeq ($(strip $(MODULE_SRAM_ENABLE)), yes)
    OPT_DEFS += -DMODULE_SRAM_ENABLE
    SRC += keyboards/keychron/common/module/module_sram.c
endif
