#!/usr/bin/env python3
"""Sync Renode .resc addresses with the built firmware ELF symbols.

This script regenerates the entire macro reset block + USB hooks + USART2
enable block in q3_max.resc using a template, then resolves all addresses
from the freshly built firmware ELF and substitutes them in. This is more
robust than in-place regex replacement, which is prone to cascading
matches when prior builds left the file in a partially-rewritten state.
"""
import re
import sys
import subprocess
import os
from pathlib import Path

ROOT = Path(os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
sys.path.insert(0, str(ROOT / "emulator/scenarios/lib"))
from symbol_resolver import resolve_symbols  # noqa: E402

ELF = ROOT / ".build/keychron_q3_max_ansi_encoder_keychron.elf"
syms = resolve_symbols(ELF)

# Symbols we need beyond the existing set.
extra_needed = ["g_module_sram", "g_emu_module_cmd", "matrix"]
nm_out = subprocess.run(
    ["arm-none-eabi-nm", str(ELF)], capture_output=True, text=True
).stdout
for name in extra_needed:
    for line in nm_out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-1] == name:
            syms[name] = "0x" + parts[0]
            break
for name in extra_needed:
    if name not in syms:
        print(f"WARNING: symbol {name!r} not found in {ELF.name}")

# Find press_key hook address: instruction after the bl debounce in matrix_scan.
objdump = subprocess.run(
    ["arm-none-eabi-objdump", "-d", str(ELF)], capture_output=True, text=True
).stdout
in_ms = False
bl_addr = None
for line in objdump.splitlines():
    if "<matrix_scan>:" in line:
        in_ms = True
        continue
    if in_ms and "debounce>" in line:
        bl_addr = int(line.split(":")[0].strip(), 16) + 4
        break

# Template the entire .resc body. Keep the file header (lines 1-15) intact
# but rewrite from the macro definition onward, so address drift can never
# corrupt anything that came before.
TEMPLATE = """macro reset
\"\"\"
    sysbus LoadELF $bin

    # F401CC OTP: F_SIZE half-word at 0x1FFF7A22 reports flash size in KB.
    # F401CC = 256 KB → 0x0100. Without this, ChibiOS efl_lld_init() leaves
    # the flash descriptor unset and backing_store_init() panics.
    sysbus WriteWord 0x1FFF7A22 0x0100

    # Patch out chSysPolledDelayX ({chSysPolledDelayX}). ChibiOS busy-loops on
    # DWT CYCCNT (at 0xE0001004) for short delays; Renode's Cortex-M
    # model does not implement DWT, so reads return 0 and the loop
    # spins forever. We replace the first instruction with `bx lr` so
    # the delay returns immediately.
    sysbus WriteWord {chSysPolledDelayX} 0x4770

    # Patch out spiSend ({spiSend}) and spiExchange ({spiExchange}).
    # ChibiOS SPI HAL kicks off DMA-driven transfers then blocks the
    # calling thread on a binary semaphore signalled by the DMA TC ISR.
    # Renode's DMA model does not generate completion IRQs, so the
    # thread sleeps forever. We make both functions `movs r0, #0; bx lr`.
    sysbus WriteDoubleWord {spiSend} 0x47702000
    sysbus WriteDoubleWord {spiExchange} 0x47702000

    # Patch out rtc_enter_init ({rtc_enter_init}). ChibiOS RTC HAL polls
    # RTC_ISR.INITF forever in the Renode-modeled RTC. Make it a no-op.
    sysbus WriteWord {rtc_enter_init} 0x4770
\"\"\"
runMacro $reset

# Enable USART2 for firmware dprintf (RCC APB1ENR USART2EN + CR1 UE/TE).
sysbus WriteDoubleWord 0x40023840 0x00020000
sysbus WriteDoubleWord 0x4000440c 0x0000200c

# Workaround: Renode's NVIC model returns RETTOBASE (ICSR bit 11) = 0
# unconditionally. We patch the bit on every ICSR read.
sysbus SetHookAfterPeripheralRead sysbus.nvic \"\"\"
if offset == 0xD04:
    value = value | 0x800
\"\"\"

# Force USBD1.state = USB_ACTIVE on every entry to usb_endpoint_in_send.
cpu AddHook {usb_endpoint_in_send} \"\"\"
sb = self.GetMachine()['sysbus']
sb.WriteByte({USBD1}, 4)
sb.WriteByte({USBD1} + 0x4F, 1)
\"\"\"

# Capture HID report bytes at obqWriteTimeout.
sysbus.cpu AddHook {obqWriteTimeout} \"\"\"
sb = self.GetMachine()['sysbus']
ep = self.GetRegisterUnsafe(0).RawValue
ptr = self.GetRegisterUnsafe(1).RawValue
sz = self.GetRegisterUnsafe(2).RawValue
if sz > 0 and sz <= 64:
    bytes_str = ' '.join(['%02x' % sb.ReadByte(ptr + i) for i in range(sz)])
    self.Log(LogLevel.Warning, 'HID-EP-WRITE ep=0x%x sz=%d: %s' % (ep, sz, bytes_str))
\"\"\"
""".format(**syms)

# Preserve the file header up to and including `showAnalyzer sysbus.usart2`.
resc_path = ROOT / "emulator/renode/q3_max.resc"
with open(resc_path) as f:
    original = f.read()

# Anchor: keep everything up to and including the showAnalyzer line.
header_end = original.find("showAnalyzer sysbus.usart2")
if header_end < 0:
    print("ERROR: cannot find anchor in q3_max.resc")
    sys.exit(1)
header_end = original.find("\n", header_end) + 1
header = original[:header_end]

new_content = header + "\n" + TEMPLATE
with open(resc_path, "w") as f:
    f.write(new_content)

# Update build-dependent SRAM addresses embedded in inline PythonPeripheral
# scripts in q3_max.repl. These cannot be symbolic at Renode runtime, so
# regenerate them from the current ELF too.
repl_path = ROOT / "emulator/renode/q3_max.repl"
with open(repl_path) as f:
    repl = f.read()
repl = re.sub(
    r"sb\.WriteByte\(0x[0-9a-fA-F]+, 4\)\s*# USBD1\.state = USB_ACTIVE",
    f"sb.WriteByte({syms['USBD1']}, 4)         # USBD1.state = USB_ACTIVE",
    repl,
)
repl = re.sub(
    r"sb\.WriteByte\(0x[0-9a-fA-F]+ \+ 0x4F, 1\)\s*# USBD1\.configuration = 1",
    f"sb.WriteByte({syms['USBD1']} + 0x4F, 1)  # USBD1.configuration = 1",
    repl,
)
with open(repl_path, "w") as f:
    f.write(repl)

# Update symbol_resolver EXPECTED_ADDRESSES
sr_path = ROOT / "emulator/scenarios/lib/symbol_resolver.py"
with open(sr_path) as f:
    sr = f.read()
for var in [
    "chSysPolledDelayX",
    "spiSend",
    "spiExchange",
    "rtc_enter_init",
    "usb_endpoint_in_send",
    "obqWriteTimeout",
    "USBD1",
]:
    sr = re.sub(f'"{var}": "0x[0-9a-fA-F]+"', f'"{var}": "{syms[var]}"', sr)
with open(sr_path, "w") as f:
    f.write(sr)

# Update renode_driver.py press_key hook
if bl_addr is not None:
    rd_path = ROOT / "emulator/scenarios/lib/renode_driver.py"
    with open(rd_path) as f:
        rd = f.read()
    rd = re.sub(r"0x0801e[12][a-f0-9]+", f"0x{bl_addr:08x}", rd)
    if "matrix" in syms:
        rd = re.sub(r"cooked = 0x[0-9a-fA-F]+", f"cooked = {syms['matrix']}", rd)
    with open(rd_path, "w") as f:
        f.write(rd)

print(f"Synced addresses for {ELF.name}")
print(
    f"  Boot patches: chSysPolledDelayX={syms['chSysPolledDelayX']}, "
    f"spiSend={syms['spiSend']}, spiExchange={syms['spiExchange']}, "
    f"rtc_enter_init={syms['rtc_enter_init']}"
)
print(
    f"  USB hooks: usb_endpoint_in_send={syms['usb_endpoint_in_send']}, "
    f"obqWriteTimeout={syms['obqWriteTimeout']}"
)
print(f"  USBD1={syms['USBD1']}")
if bl_addr is not None:
    print(f"  press_key hook: 0x{bl_addr:08x}")
print(f"  g_module_sram={syms.get('g_module_sram', '?')}")
print(f"  g_emu_module_cmd={syms.get('g_emu_module_cmd', '?')}")
print(f"  matrix={syms.get('matrix', '?')}")
