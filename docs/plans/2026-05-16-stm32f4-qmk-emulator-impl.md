# STM32F4 QMK Hardware Emulator Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a Renode-based emulator that boots the unmodified Keychron Q3 Max QMK firmware (STM32F401), stubs the LKBT51 SPI co-MCU, injects matrix events, and captures HID reports.

**Architecture:** Renode runs the ELF; mainline STM32F4 peripherals cover core+clocks+GPIO+timers+SPI+flash; Python peripherals stub LKBT51 and inject matrix events from `info.json`; a minimal OTG FS device extension plus internal fake-host shim carries the firmware to USB-configured state; a Python HID logger captures and decodes emitted reports.

**Tech Stack:** Renode (1.15+), C# (Renode peripheral plugins), Python 3.10+, ChibiOS HAL (firmware-side, unchanged), QMK build system (unchanged).

**Design doc:** `docs/plans/2026-05-16-stm32f4-qmk-emulator-design.md`

**Style note:** Python work uses TDD (red/green/commit per task). Renode `.repl`/`.resc` and C# OTG FS work use checklist+verification style (verify-by-observation, since Renode integration tests are the unit of testability). Commit after every green checkpoint.

---

## Phase 0 — Renode Sanity & Toolchain (≈ 2 days)

**Exit criterion:** Renode opens, loads the Q3 Max ELF, CPU executes past the reset handler without faulting.

### Task 0.1 — Pin tooling versions

**Files:**
- Create: `emulator/README.md`
- Create: `emulator/.tool-versions`

**Steps:**

1. Install Renode 1.15.x locally; record exact version: `renode --version > /tmp/renode-ver.txt`.
2. Write `emulator/.tool-versions`:
   ```
   renode 1.15.3   # or whatever was installed
   python 3.10
   ```
3. Write `emulator/README.md` skeleton: project goal, dependencies, how to run scenarios (placeholder), pointer to design doc.
4. Commit:
   ```
   git add emulator/README.md emulator/.tool-versions
   git commit -m "emulator: scaffold README and tool-version pin"
   ```

### Task 0.2 — Build the Q3 Max ELF locally

**Verification:**

1. Run: `qmk compile -kb keychron/q3_max/ansi_encoder -km keychron`
2. Confirm the `.elf` and `.bin` exist under `.build/`.
3. Note exact path: `ls -la .build/keychron_q3_max_ansi_encoder_keychron.elf`
4. Record the path in `emulator/README.md` under "Building firmware."
5. Commit README update only:
   ```
   git add emulator/README.md
   git commit -m "emulator: document firmware build invocation"
   ```

### Task 0.3 — Minimal Renode platform & boot script

**Files:**
- Create: `emulator/renode/q3_max.repl`
- Create: `emulator/renode/q3_max.resc`

**Steps:**

1. Write `q3_max.repl` declaring only the CPU + memory:
   ```
   using "platforms/cpus/stm32f401.repl"
   ```
   (Reuse Renode mainline F401 base; if it doesn't exist, copy `platforms/cpus/stm32f4.repl` and adjust memory sizes: flash 256K @ 0x08000000, SRAM 64K @ 0x20000000.)

2. Write `q3_max.resc`:
   ```
   :name: q3_max
   :description: Keychron Q3 Max emulator

   $bin?=@.build/keychron_q3_max_ansi_encoder_keychron.elf

   using sysbus
   mach create "q3_max"
   machine LoadPlatformDescription @emulator/renode/q3_max.repl

   showAnalyzer sysbus.usart2

   macro reset
   """
       sysbus LoadELF $bin
   """
   runMacro $reset
   ```

3. Verification: `renode emulator/renode/q3_max.resc` → window opens, no error on load.
4. Type `start` in Renode monitor; let it run 5 seconds; type `pause`; type `cpu PC` — confirm PC > 0x08000000 + reset handler offset. Don't worry yet if it has faulted; we just want "code runs."
5. Commit:
   ```
   git add emulator/renode/
   git commit -m "emulator: minimal Renode platform loads Q3 Max ELF"
   ```

### Task 0.4 — Phase 0 retrospective

**Steps:**

1. Open `emulator/docs/boot_path.md` (create).
2. Record: Renode version, ELF size, observed initial PC, first ~10 PC values after reset (`cpu PC` repeatedly), any faults seen.
3. Commit.

---

## Phase 1 — Boot Through Stage 2 (≈ 4–6 days)

**Exit criterion:** Firmware reaches ChibiOS `main()` and emits QMK boot text over emulated USART2.

### Task 1.1 — Identify exact peripherals firmware touches at boot

**Verification:**

1. Run Renode with logging: `logLevel 0 sysbus` — full bus access trace.
2. Let firmware run for 1 second; pause; save log to `/tmp/q3_max-boot.log`.
3. Extract unique peripheral base addresses accessed: `grep -oE '0x[4567][0-9A-F]+' /tmp/q3_max-boot.log | sort -u > /tmp/q3_max-peripherals.txt`.
4. Map addresses to STM32F401 peripheral names (RM0368 reference manual).
5. Append findings to `emulator/docs/boot_path.md`.
6. Commit doc update.

### Task 1.2 — Confirm RCC PLL ready handshake works

**Verification:**

1. Place breakpoint at ChibiOS `stm32_clock_init` end (find symbol: `arm-none-eabi-nm .build/keychron_q3_max_ansi_encoder_keychron.elf | grep stm32_clock_init`).
2. In Renode: `sysbus.cpu AddHook <addr> "self.Log(LogLevel.Info, 'clock init done')"`.
3. Run; expect to see the log message within ~50 ms simulated time.
4. If the hook never fires → RCC PLL ready bit is the culprit; patch in next task. Document outcome in `boot_path.md`.

### Task 1.3 — Patch RCC if needed

**Trigger:** Only if Task 1.2 hangs.

**Files:**
- Possibly modify: Renode RCC peripheral C# source (in Renode install tree, applied via overlay)
- Alternative: add explicit register-init via `.resc` `sysbus WriteDoubleWord` calls

**Steps:**

1. Inspect Renode's STM32F4 RCC model: which bits in `RCC_CR` does it auto-flip on `HSEON`/`PLLON` writes?
2. If PLLRDY doesn't latch quickly enough, add to `q3_max.resc` reset macro:
   ```
   sysbus WriteDoubleWord 0x40023800 0x03035083   # RCC_CR: HSE+PLL on, ready bits set
   ```
   (Use real values from RM0368.) This is a hack-workaround; flag in `boot_path.md` for proper fix in v1.1.
3. Re-run Task 1.2 verification.
4. Commit.

### Task 1.4 — UART output proof

**Steps:**

1. Confirm `q3_max.repl` includes USART2; if not, add (mainline STM32F4 repls have it).
2. Confirm `showAnalyzer sysbus.usart2` in `q3_max.resc`.
3. Run firmware to free-run for 2 seconds.
4. Expect to see QMK boot banner / printf output in the analyzer window.
5. If silent: check `CONSOLE_ENABLE` — may need the emu-diagnostic build (Task 1.5).
6. Screenshot or text-capture; append to `boot_path.md`.
7. Commit.

### Task 1.5 — Emu-diagnostic build profile

**Files:**
- Create: `emulator/build/rules.mk`
- Create: `emulator/build/README.md`
- Create: `emulator/build/build_emu.sh` (convenience script)

**Steps:**

1. Write `emulator/build/rules.mk`:
   ```make
   # Diagnostic build flags for emulator targets.
   # Include via:
   #   qmk compile -kb keychron/q3_max/ansi_encoder -km keychron -e EXTRA_RULES=emulator/build/rules.mk
   CONSOLE_ENABLE = yes
   DEBUG_MATRIX_SCAN_RATE_ENABLE = yes
   OPT_DEFS += -DEMULATOR_BUILD
   ```
2. Write `build_emu.sh` invoking qmk with the right flags (research the exact `qmk compile` flag for extra rules; alternative: env var `EXTRAFLAGS`).
3. Run it; confirm a different ELF is produced (different size).
4. Re-run Renode with the new ELF; expect richer UART output.
5. Commit.

### Task 1.6 — Phase 1 gate

**Steps:**

1. Append to `emulator/docs/boot_path.md`: final boot trace, peripherals required, any patches applied, UART output captured.
2. Decide: are we through Stage 2 cleanly? Document yes/no.
3. Commit.

---

## Phase 2 — LKBT51 Stub & Matrix Injector (≈ 4–6 days)

**Style:** Python = TDD. Renode wiring = checklist.

### Task 2.1 — `layout.py` (TDD, pure Python)

**Files:**
- Create: `emulator/scenarios/lib/layout.py`
- Create: `emulator/scenarios/lib/__init__.py`
- Create: `emulator/scenarios/lib/test_layout.py`

**Step 1: Write failing test:**

```python
# emulator/scenarios/lib/test_layout.py
from pathlib import Path
from layout import Layout

INFO = Path(__file__).resolve().parents[3] / "keyboards/keychron/q3_max/info.json"

def test_coords_for_kc_h():
    layout = Layout.from_info_json(INFO, variant="ansi_encoder")
    row, col = layout.coords("KC_H")
    assert isinstance(row, int) and isinstance(col, int)
    assert 0 <= row < 32 and 0 <= col < 32

def test_all_keys_nonempty():
    layout = Layout.from_info_json(INFO, variant="ansi_encoder")
    keys = layout.all_keys()
    assert len(keys) > 50  # full-size keyboard

def test_missing_key_raises():
    import pytest
    layout = Layout.from_info_json(INFO, variant="ansi_encoder")
    with pytest.raises(KeyError):
        layout.coords("KC_NOT_A_REAL_KEY")
```

**Step 2:** Run: `python -m pytest emulator/scenarios/lib/test_layout.py -v` → expect ImportError.

**Step 3:** Inspect `keyboards/keychron/q3_max/info.json` structure. Look for `layouts.<variant>.layout[].matrix` and `.label`. Note: keymap.c default keymap maps KC_* to matrix positions — `info.json` doesn't directly give "KC_H → (row,col)". We need to read the **default keymap** from `keyboards/keychron/q3_max/keymaps/keychron/keymap.c`.

Revise design: `Layout` reads both `info.json` (layout positions) AND the default keymap (KC_* assignments at layer 0). For ANSI variant, layer 0 default keymap is the standard QWERTY.

**Step 4:** Implement `layout.py`:

```python
# emulator/scenarios/lib/layout.py
import json
import re
from pathlib import Path
from typing import NamedTuple

class Layout:
    def __init__(self, key_to_coord: dict[str, tuple[int, int]],
                 row_pins: list[str], col_pins: list[str]):
        self._k2c = key_to_coord
        self.row_pins = row_pins
        self.col_pins = col_pins

    @classmethod
    def from_info_json(cls, info_path: Path, variant: str) -> "Layout":
        info = json.loads(Path(info_path).read_text())
        layouts = info["layouts"]
        # variant may be "ansi_encoder" or "LAYOUT_ansi_xx" — try both
        layout_key = next(
            (k for k in layouts if variant in k or k.endswith(variant)),
            None,
        )
        if layout_key is None:
            raise KeyError(f"variant {variant} not found in {list(layouts)}")
        positions = layouts[layout_key]["layout"]  # list of dicts with "matrix": [row,col]

        # Read default keymap to map KC_X → matrix position via index
        # Path: keyboards/keychron/q3_max/<variant>/keymaps/keychron/keymap.c
        keymap_path = info_path.parent / variant / "keymaps" / "keychron" / "keymap.c"
        if not keymap_path.exists():
            # Fallback: scan for any keymap.c under the variant dir
            candidates = list((info_path.parent / variant).rglob("keymap.c"))
            if not candidates:
                raise FileNotFoundError(f"no keymap.c near {keymap_path}")
            keymap_path = candidates[0]

        keycodes_at_layer0 = cls._parse_layer0(keymap_path)
        if len(keycodes_at_layer0) != len(positions):
            raise ValueError(
                f"keymap has {len(keycodes_at_layer0)} keys, "
                f"layout has {len(positions)} positions"
            )

        k2c = {}
        for kc, pos in zip(keycodes_at_layer0, positions):
            r, c = pos["matrix"]
            k2c.setdefault(kc, (r, c))  # first occurrence wins

        # Pin info
        row_pins = info["matrix_pins"]["rows"]
        col_pins = info["matrix_pins"]["cols"]
        return cls(k2c, row_pins, col_pins)

    @staticmethod
    def _parse_layer0(keymap_c: Path) -> list[str]:
        """Extract KC_* tokens from the first LAYOUT_*(...) call in keymap.c."""
        text = keymap_c.read_text()
        # Find first LAYOUT(...) macro call; grab everything between matched parens
        m = re.search(r"LAYOUT[A-Za-z0-9_]*\s*\(", text)
        if not m:
            raise ValueError(f"no LAYOUT(...) macro in {keymap_c}")
        depth = 0
        start = m.end() - 1
        for i in range(start, len(text)):
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0:
                    body = text[start + 1 : i]
                    break
        tokens = [t.strip() for t in body.split(",")]
        # Strip whitespace, comments, and macro wrappers like MO(2) → keep as-is for now
        return [t for t in tokens if t]

    def coords(self, key: str) -> tuple[int, int]:
        if key not in self._k2c:
            raise KeyError(key)
        return self._k2c[key]

    def all_keys(self) -> list[str]:
        return list(self._k2c.keys())
```

**Step 5:** Run pytest; iterate until green. Expect issues with keymap path/variant naming — adjust based on actual repo layout.

**Step 6:** Commit:
```
git add emulator/scenarios/lib/
git commit -m "emulator: Layout reads matrix coords from info.json + keymap.c"
```

### Task 2.2 — LKBT51 stub (TDD against protocol)

**Files:**
- Create: `emulator/renode/peripherals/lkbt51_stub.py`
- Create: `emulator/renode/peripherals/test_lkbt51_stub.py`

**Step 1: Write failing tests** that exercise the protocol decoder *without* Renode (Renode hosts the peripheral, but the protocol logic is pure Python and unit-testable):

```python
# test_lkbt51_stub.py
from lkbt51_stub import Lkbt51Protocol

CMD_GET_MODULE_INFO = 0x40
CMD_READ_STATE_REG = 0x25
CMD_SEND_KB = 0x11

def test_get_module_info_returns_versioned_blob():
    p = Lkbt51Protocol()
    reply = p.handle_command(CMD_GET_MODULE_INFO, payload=b"")
    assert len(reply) > 0
    # Convention: first byte ack, followed by version info
    assert reply[0] == 0x00  # ACK

def test_read_state_reg_reports_no_host_paired():
    p = Lkbt51Protocol()
    reply = p.handle_command(CMD_READ_STATE_REG, payload=b"")
    # State byte 0 = no host connection (bit layout per ckbt51 driver)
    assert reply[0] == 0x00
    assert (reply[1] & 0x01) == 0  # no_host_paired bit clear

def test_send_kb_is_acked_and_discarded():
    p = Lkbt51Protocol()
    reply = p.handle_command(CMD_SEND_KB, payload=bytes([0, 0, 0x0B, 0, 0, 0, 0, 0]))  # 'h'
    assert reply == b"\x00"  # bare ACK

def test_unknown_command_acks():
    p = Lkbt51Protocol()
    reply = p.handle_command(0xFE, payload=b"")
    assert reply[0] == 0x00
```

**Step 2:** Run, expect ImportError.

**Step 3:** Implement `Lkbt51Protocol` class in `lkbt51_stub.py`. Read `keyboards/keychron/common/wireless/lkbt51.c` to confirm exact reply formats:

```python
# lkbt51_stub.py (protocol-only; Renode integration wrapper at bottom)
class Lkbt51Protocol:
    CMD_GET_MODULE_INFO = 0x40
    CMD_READ_STATE_REG = 0x25
    CMD_HAND_SHAKE_TOKEN = 0x61
    CMD_SEND_KB = 0x11
    CMD_SEND_KB_NKRO = 0x12
    CMD_SEND_CONSUMER = 0x13
    CMD_SEND_SYSTEM = 0x14
    CMD_SEND_FN = 0x15
    CMD_SEND_MOUSE = 0x16
    CMD_SEND_BOOT_KB = 0x17
    CMD_BATTERY_MANAGE = 0x31
    CMD_UPDATE_BAT_LVL = 0x32
    CMD_UPDATE_BAT_STATE = 0x33

    def handle_command(self, cmd: int, payload: bytes) -> bytes:
        if cmd == self.CMD_GET_MODULE_INFO:
            # Fake fw_ver=1.0, hw_ver=1.0
            return bytes([0x00, 0x01, 0x00, 0x01, 0x00])
        if cmd == self.CMD_READ_STATE_REG:
            # ACK + state bytes; all zero = idle, wired, no pairing
            return bytes([0x00, 0x00, 0x00, 0x00])
        if cmd == self.CMD_HAND_SHAKE_TOKEN:
            return bytes([0x00, 0x55, 0xAA])  # echo plausible token
        if 0x11 <= cmd <= 0x17:
            return bytes([0x00])  # ACK key report
        if 0x31 <= cmd <= 0x33:
            return bytes([0x00, 0x64])  # ACK + 100% battery
        return bytes([0x00])  # default ACK
```

**Step 4:** Run pytest; iterate to green.

**Step 5:** Commit:
```
git add emulator/renode/peripherals/lkbt51_stub.py emulator/renode/peripherals/test_lkbt51_stub.py
git commit -m "emulator: LKBT51 protocol stub (Python, unit-tested)"
```

### Task 2.3 — LKBT51 Renode integration wrapper

**Files:**
- Modify: `emulator/renode/peripherals/lkbt51_stub.py` (add Renode wrapper)
- Modify: `emulator/renode/q3_max.repl` (declare SPI1 slave)
- Modify: `emulator/renode/q3_max.resc` (load Python peripheral)

**Steps:**

1. Read Renode Python-peripheral docs (e.g. `PythonPeripheral`, `IGPIOReceiver`, `ISPIPeripheral`). Confirm available interfaces in pinned Renode version.
2. Add Renode wrapper in `lkbt51_stub.py`:
   ```python
   # Renode injects: self, request, Request namespace, Log, LogLevel
   from Antmicro.Renode.Peripherals.SPI import ISPIPeripheral
   _proto = Lkbt51Protocol()

   def Transmit(request):
       # request.value is the byte from master
       # Drive command/response state machine
       ...
   ```
   Exact API depends on Renode version. Refer to `renode/src/Plugins/...` examples in Renode source.
3. Wire SPI1 in `.repl`:
   ```
   spi1: SPI.STM32SPI @ sysbus 0x40013000
       IRQ -> nvic@35
   lkbt51: Python.PythonSPIPeripheral @ spi1
       script: @emulator/renode/peripherals/lkbt51_stub.py
   ```
4. Wire B1 GPIO output from peripheral to NVIC EXTI in `.repl`.
5. Boot firmware. Verify (via `boot_path.md` notes) that wireless init completes without the 2000 ms retry loop.
6. Verification: log every SPI byte the master sent and the peripheral's reply for the first 10 transactions. Append to `boot_path.md`.
7. Commit.

### Task 2.4 — Matrix injector (TDD for logic, checklist for wiring)

**Files:**
- Create: `emulator/renode/peripherals/matrix_injector.py`
- Create: `emulator/renode/peripherals/test_matrix_injector.py`

**Step 1: Failing test for the pure-Python state logic:**

```python
# test_matrix_injector.py
from matrix_injector import MatrixState

def test_initial_no_keys_pressed():
    m = MatrixState(rows=5, cols=14, diode_direction="col2row")
    # No keys pressed: scanning col 0 → all rows read inactive
    m.set_column_driven(0, active=True)
    assert m.read_row(0) is False
    assert m.read_row(1) is False

def test_press_makes_row_active_when_correct_col_driven():
    m = MatrixState(rows=5, cols=14, diode_direction="col2row")
    m.press(row=2, col=5)
    m.set_column_driven(5, active=True)
    assert m.read_row(2) is True
    assert m.read_row(0) is False

def test_press_does_not_affect_other_columns():
    m = MatrixState(rows=5, cols=14, diode_direction="col2row")
    m.press(row=2, col=5)
    m.set_column_driven(6, active=True)
    assert m.read_row(2) is False

def test_release_clears_press():
    m = MatrixState(rows=5, cols=14, diode_direction="col2row")
    m.press(row=2, col=5)
    m.release(row=2, col=5)
    m.set_column_driven(5, active=True)
    assert m.read_row(2) is False
```

**Step 2:** Run, expect ImportError.

**Step 3:** Implement `MatrixState`:

```python
class MatrixState:
    def __init__(self, rows: int, cols: int, diode_direction: str = "col2row"):
        self.rows = rows
        self.cols = cols
        self.diode_direction = diode_direction
        self._pressed: set[tuple[int, int]] = set()
        self._col_active = [False] * cols

    def press(self, row: int, col: int) -> None:
        self._pressed.add((row, col))

    def release(self, row: int, col: int) -> None:
        self._pressed.discard((row, col))

    def set_column_driven(self, col: int, active: bool) -> None:
        self._col_active[col] = active

    def read_row(self, row: int) -> bool:
        for c in range(self.cols):
            if self._col_active[c] and (row, c) in self._pressed:
                return True
        return False
```

**Step 4:** Run pytest, iterate to green.

**Step 5:** Add Renode wrapper (separate class in same file) that reads `MATRIX_ROW_PINS`/`MATRIX_COL_PINS` from `info.json`, registers as GPIO listener on column pins, drives row pins. Wire in `q3_max.repl`.

**Step 6:** Boot firmware with matrix injector. Inject `press(KC_H)` via Renode monitor command (define a macro in `q3_max.resc`). Check `DEBUG_MATRIX_SCAN_RATE` log shows the press.

**Step 7:** Commit:
```
git add emulator/renode/peripherals/matrix_injector.py emulator/renode/peripherals/test_matrix_injector.py emulator/renode/q3_max.repl emulator/renode/q3_max.resc
git commit -m "emulator: matrix injector with TDD state model + Renode wiring"
```

### Task 2.5 — `boot_smoke.py` scenario

**Files:**
- Create: `emulator/scenarios/boot_smoke.py`
- Create: `emulator/scenarios/lib/renode_driver.py`

**Steps:**

1. Implement `renode_driver.py` as a thin wrapper around Renode's robot interface or `pyrenode3` package (research what's available in pinned version).
2. Write `boot_smoke.py`:
   ```python
   from lib.renode_driver import Renode
   from lib.layout import Layout
   from pathlib import Path

   ELF = ".build/keychron_q3_max_ansi_encoder_keychron.elf"
   INFO = "keyboards/keychron/q3_max/info.json"

   with Renode("emulator/renode/q3_max.resc", elf=ELF) as r:
       r.wait_for_uart(r"QMK", timeout_s=10)
       layout = Layout.from_info_json(INFO, variant="ansi_encoder")
       row, col = layout.coords("KC_H")
       r.matrix_press(row, col)
       r.run_for_ms(50)
       r.matrix_release(row, col)
       r.run_for_ms(50)
       # Phase 2 success = no hang, UART showed scan-rate activity
       print("boot_smoke: OK")
   ```
3. Run: `python emulator/scenarios/boot_smoke.py`. Expect "boot_smoke: OK".
4. Commit.

---

## Phase 3 — OTG FS Minimal + Fake Host (≈ 6–10 days; cut-line 12 days)

**Style:** Checklist with verification. C# peripheral work in Renode.

### Task 3.1 — Survey Renode mainline OTG FS state

**Steps:**

1. Locate Renode source: `find / -path '*/Renode/*OTG*' 2>/dev/null` or clone Renode from GitHub.
2. Read existing STM32 USB OTG device peripheral. List supported registers and missing pieces.
3. Append findings to `emulator/otg_fs/SURVEY.md`.
4. Decide: extend in-place via overlay, or fork file into `emulator/otg_fs/Stm32OtgFsDevice.cs`.
5. Commit survey.

### Task 3.2 — Stand up OTG FS device with EP0 control transfers

**Files:**
- Create: `emulator/otg_fs/Stm32OtgFsDevice.cs`
- Modify: `emulator/renode/q3_max.repl`

**Verification gates:**

1. Firmware writes to `GUSBCFG`, `DCFG`, etc. without bus faults.
2. After firmware calls `usbConnectBus`, our model emits a `RESET` interrupt.
3. After our internal fake host issues `GET_DESCRIPTOR(device)` via EP0, firmware responds with the QMK descriptor blob.
4. Firmware acknowledges `SET_ADDRESS(N)` and stores N in `DCFG.DAD`.

Append observed traces to `emulator/otg_fs/SURVEY.md` at each gate.

### Task 3.3 — Internal fake-host shim

**Files:**
- Create: `emulator/otg_fs/InternalFakeHost.cs`

**Steps:**

1. State machine: RESET → wait → GET_DESCRIPTOR(device) → GET_DESCRIPTOR(config) → SET_ADDRESS → GET_DESCRIPTOR(strings) → SET_CONFIGURATION(1) → "configured."
2. Log every step to Renode log.
3. After "configured": fake host polls each IN endpoint at the interval declared in the endpoint descriptor.
4. Verification: UART shows QMK reach steady-state main loop with USB configured. Inject a key; firmware emits HID report bytes into IN-EP1 FIFO.
5. Commit.

### Task 3.4 — Phase 3 gate check

**Steps:**

1. If 12 working days exceeded and not at "configured" → activate fallback (Task 4-alt below).
2. Document outcome in `emulator/docs/boot_path.md`.

---

## Phase 4 — HID Report Logger (≈ 3–4 days)

### Task 4.1 — Report decoder (TDD)

**Files:**
- Create: `emulator/scenarios/lib/hid_decode.py`
- Create: `emulator/scenarios/lib/test_hid_decode.py`

**Tests:**

```python
from hid_decode import decode_keyboard_report, decode_nkro_report

def test_keyboard_6kro_h_pressed():
    # Standard 8-byte boot report: mods, reserved, key1..key6
    report = bytes([0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00])
    keys = decode_keyboard_report(report)
    assert keys == ["KC_H"]

def test_keyboard_with_shift_modifier():
    report = bytes([0x02, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00])  # left-shift + H
    keys = decode_keyboard_report(report)
    assert "KC_LSFT" in keys
    assert "KC_H" in keys

def test_nkro_bitmap_h_pressed():
    # NKRO: report ID + modifier byte + bitmap
    bitmap = bytearray(16)
    bitmap[0x0B // 8] |= 1 << (0x0B % 8)
    report = bytes([0x06, 0x00]) + bytes(bitmap)  # report ID 6 = NKRO
    keys = decode_nkro_report(report)
    assert keys == ["KC_H"]
```

**Implementation:** Standard HID Usage ID → KC_* lookup table. Reference USB HID Usage Tables 1.12 §10.

**Commit per green test.**

### Task 4.2 — Logger Renode peripheral

**Files:**
- Create: `emulator/renode/peripherals/hid_report_logger.py`

**Steps:**

1. Hooks OTG FS IN-EP FIFO writes (provided by Phase 3 model).
2. For each completed packet: identify EP number, route to decoder, log decoded report to stdout with timestamp.
3. Also write raw bytes to `emulator/traces/<scenario>_reports.log` for diffing.
4. Commit.

### Task 4.3 — `type_hello.py` scenario

**Files:**
- Create: `emulator/scenarios/type_hello.py`
- Create: `emulator/scenarios/lib/report_assertions.py`

**Implementation:** Inject H-E-L-L-O matrix presses with proper hold/release timing; capture reports; assert sequence:

```python
assert_keypress_sequence(reports, ["KC_H", "KC_E", "KC_L", "KC_L", "KC_O"])
```

Note: typing 'hello' actually emits two `KC_L` reports with a key-up between, because consecutive same-key presses need release-then-press. The assertion helper handles this.

**Commit when green.**

### Task 4-alt — Fallback if Phase 3 cut-line hit

**Trigger:** Phase 3 didn't reach "configured."

**Files:**
- Create: `emulator/scenarios/lib/symbol_capture.py`

**Approach:** Use ELF debug info; hook QMK's `host_keyboard_send` (or equivalent symbol) directly via Renode address breakpoint. Capture the report bytes from registers/memory at the hook point, bypassing USB entirely. Less faithful but unblocks Phase 4–5.

**Commit fallback path; v2.0 picks up the proper USB path.**

---

## Phase 5 — Polish, Docs, Second Scenario (≈ 3–4 days)

### Task 5.1 — `combo_test.py`

**Files:**
- Create: `emulator/scenarios/combo_test.py`

**Steps:**

1. Identify a combo configured in your current keymap (sticky_combo J+K per recent commits).
2. Scenario: press J and K within `COMBO_TERM` ms → assert the combo output appears in HID reports, not raw J and K.
3. Negative case: press J and K with > `COMBO_TERM` ms gap → assert raw J then raw K.
4. Commit.

### Task 5.2 — Documentation pass

**Files:**
- Modify: `emulator/README.md` (full quickstart)
- Create: `emulator/docs/troubleshooting.md`
- Modify: `emulator/docs/boot_path.md` (final tidy)

**Contents of troubleshooting.md:** All failure modes hit during Phases 0–4, with symptom → diagnosis → fix.

**Commit.**

### Task 5.3 — Compare with real-hardware USBmon trace

**Steps:**

1. Place real-hardware capture at `emulator/traces/real_hw_usbmon.pcapng`.
2. Capture emulator-side trace of the same enumeration sequence (e.g. via Phase 3 logging).
3. Diff descriptor exchanges; document divergences in `emulator/docs/troubleshooting.md` (these become v2.0 fix list).
4. Commit.

### Task 5.4 — Final sweep

**Steps:**

1. Run all three scenarios from a clean checkout: `boot_smoke.py`, `type_hello.py`, `combo_test.py`. All green.
2. Verify zero modifications outside `emulator/`: `git diff --stat main -- ':!emulator/' ':!docs/plans/'` should show no QMK source changes.
3. Tag the release: `git tag emulator-v1.0`.
4. Final commit / push.

---

## Risks & Fallbacks Reference

| Phase | Risk | Fallback |
|---|---|---|
| 1 | RCC PLL ready timing | Hard-write RCC_CR via `.resc` |
| 2 | LKBT51 protocol misread | Capture real-HW SPI trace (logic analyzer) |
| 3 | OTG FS won't configure | Symbol-level report capture (Task 4-alt) |
| 4 | NKRO report-ID mismatch | Add explicit endpoint→decoder mapping table |
| 5 | Scenario flake | Add `wait_for_uart_quiet()` between phases |

## Out-of-Scope Reminder (v2.0+)

- USB/IP server + real `/dev/input/eventN` (v2.0)
- RGB matrix render-to-window (v2.1)
- Q3 HE Hall-effect ADC (v2.2)
- V3 Max (v2.3)
- Encoder rotation injection (v2.4)
- Wireless-mode HID path (v2.5)
