VIA_ENABLE = yes
KEY_BEHAVIOR_SM_ENABLE = yes
# VIM_MODAL_ENABLE intentionally disabled — would intercept J/K and
# consume them via the firmware-side modal, preventing the SRAM
# sticky-combo module from seeing the keys.
# STICKY_COMBO_ENABLE intentionally not defined — the SRAM behavior
# module (kbsm_sticky_combo) provides this feature dynamically.
MODULE_SRAM_ENABLE = yes
OPT_DEFS += -DEMULATOR_BUILD
