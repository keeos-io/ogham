# Building and flashing Ogham firmware

Step-by-step for **Windows**, **macOS** and **Linux**. If you only want to *run*
the firmware, you do not need any of this — download a `.bin` from the
[releases page](https://github.com/stevec64/keeos-ogham/releases) or use the
in-browser flasher at <https://keeos.io/firmware/ogham/>, and skip to
[Flashing](#flashing).

> **Verification note.** The Windows instructions are the ones used day to day
> and are known good. The macOS and Linux instructions follow the standard route
> for each platform and use the same official Arm toolchain, but have not been
> exercised on those machines. If something is off, please open an issue — it
> will be a documentation bug, not a firmware one.

## What you need

| | Why | Version |
|---|---|---|
| **Arm GNU Toolchain, `arm-none-eabi`** | Cross-compiler | **14.2.Rel1** — see below |
| **GNU Make** | Build driver | Any recent |
| **Git** | With submodule support | Any recent |
| *Optional:* **OpenOCD** | Flashing over SWD with an ST-Link | 0.12.x |
| *Optional:* **dfu-util** | Flashing over USB, no probe needed | 0.11+ |
| *Optional:* **Python 3** | Live telemetry tools in `firmware/tools/` | 3.9+ |

### Why the toolchain version is called out

The published binaries are byte-for-byte reproducible with **Arm GNU Toolchain
14.2.Rel1** and the submodules at their pinned commits. A different compiler
version builds perfectly good firmware — it just will not match the SHA-256
published with the release. If you are verifying a download rather than
developing, use 14.2.Rel1; otherwise use whatever you have.

Direct downloads for 14.2.Rel1 (all verified live):

| Platform | File |
|---|---|
| Windows | [`…-mingw-w64-i686-arm-none-eabi.exe`](https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-mingw-w64-i686-arm-none-eabi.exe) |
| macOS (Apple Silicon) | [`…-darwin-arm64-arm-none-eabi.tar.xz`](https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi.tar.xz) |
| macOS (Intel) | [`…-darwin-x86_64-arm-none-eabi.tar.xz`](https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-darwin-x86_64-arm-none-eabi.tar.xz) |
| Linux (x86-64) | [`…-x86_64-arm-none-eabi.tar.xz`](https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz) |
| Linux (ARM64) | [`…-aarch64-arm-none-eabi.tar.xz`](https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-aarch64-arm-none-eabi.tar.xz) |

---

## Windows

Use **Git Bash** (installed with Git for Windows) for the commands below. The
Makefiles are POSIX shell; they will not run in `cmd.exe` or PowerShell.

The exact set-up in use, via `winget`:

```powershell
winget install Arm.GnuArmEmbeddedToolchain    # 14.2.Rel1
winget install ezwinports.make                # GNU Make 4.4.1
winget install xpack-dev-tools.openocd-xpack  # optional, for ST-Link flashing
```

**Windows does not ship `make`, and the Arm toolchain does not include it** —
that is the step people miss. `ezwinports.make` is a plain native build with no
MSYS baggage. MSYS2's `make`, or Chocolatey's, work equally well.

`dfu-util` has no reliable winget package; take the Windows binary from
<https://dfu-util.sourceforge.net/> and put it on your `PATH`.

The Arm installer offers to add itself to `PATH` — say yes, or add it per shell:

```bash
export PATH="/c/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin:$PATH"
```

### USB drivers

Windows needs a driver bound to the device before flashing tools can see it:

- **ST-Link** — install ST's ST-LINK USB driver, or use the one bundled with
  STM32CubeProgrammer.
- **DFU (bootloader mode)** — the Seed enumerates as *STM32 BOOTLOADER* and
  needs the **WinUSB** driver. Use [Zadig](https://zadig.akeo.ie/): put the Seed
  in DFU mode, pick that device, select **WinUSB**, and hit Replace Driver.
  Without this, `dfu-util` reports no DFU capable devices found.

---

## macOS

With [Homebrew](https://brew.sh):

```bash
brew install --cask gcc-arm-embedded    # Arm toolchain (check the version it gives you)
brew install make git open-ocd dfu-util
```

macOS ships GNU Make 3.81, which is old but sufficient here. Homebrew's `make`
installs as `gmake` unless you adjust `PATH`; either works.

**If you need exactly 14.2.Rel1** — because you want to reproduce a published
checksum — install Arm's tarball rather than the cask, since the cask tracks
whatever is current:

```bash
curl -LO https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi.tar.xz
sudo mkdir -p /opt/arm && sudo tar -xf arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi.tar.xz -C /opt/arm
export PATH="/opt/arm/arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi/bin:$PATH"
```

Substitute `darwin-x86_64` on an Intel Mac.

**Gatekeeper.** Binaries downloaded outside the App Store are quarantined, and
the first `arm-none-eabi-gcc` run may be blocked. Clear it once:

```bash
sudo xattr -dr com.apple.quarantine /opt/arm
```

No driver work is needed — ST-Link and DFU both work through the system USB
stack.

---

## Linux

Install Make, Git and the flashing tools from your distribution:

```bash
# Debian / Ubuntu
sudo apt install build-essential git openocd dfu-util

# Fedora
sudo dnf install make git openocd dfu-util

# Arch
sudo pacman -S make git openocd dfu-util
```

For the compiler, **prefer Arm's tarball over the distro package**. Distro
`gcc-arm-none-eabi` versions vary widely, and some have historically shipped
without the newlib the build expects:

```bash
curl -LO https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz
sudo mkdir -p /opt/arm && sudo tar -xf arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz -C /opt/arm
export PATH="/opt/arm/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin:$PATH"
```

Add that `export` to your `~/.bashrc` to make it stick.

> On 64-bit-only distributions the toolchain may need 32-bit support libraries.
> If `arm-none-eabi-gcc` reports "No such file or directory" despite clearly
> existing, that is the cause — install your distro's 32-bit compatibility
> packages.

### udev rules — the step that catches everyone

Without these, flashing fails with a permissions error unless you run as root.
Create `/etc/udev/rules.d/49-ogham.rules`:

```
# ST-Link V2 / V2.1 / V3
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3748", MODE="0666", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374b", MODE="0666", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374f", MODE="0666", TAG+="uaccess"
# STM32 DFU bootloader (the Daisy Seed in bootloader mode)
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="df11", MODE="0666", TAG+="uaccess"
```

Then reload and re-plug the device:

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

---

## Building

Identical on all three platforms once the tools are in place.

```bash
git clone --recurse-submodules https://github.com/stevec64/keeos-ogham.git
cd keeos-ogham
make -C lib/libDaisy -j8      # a few minutes the first time
make -C lib/DaisySP  -j8
cd firmware && make -j8
```

Output lands in `firmware/build/`: `ogham_bytebeat.elf`, `.bin` and `.hex`.

**`--recurse-submodules` is not optional.** libDaisy has nested submodules of its
own (CMSIS, the STM32 HAL). Without them the build fails part-way with
`No rule to make target '.../stm32h7xx_hal.o'`, which does not hint at the real
cause. If you already cloned without it:

```bash
git submodule update --init --recursive
```

### Two encoder builds

Two batches of rotary encoder exist with opposite A/B phase, and the firmware
cannot tell which is fitted:

```bash
make -j8                                              # default — current encoder
make -f Makefile.encorig BUILD_DIR=build_encorig -j8  # early units
```

If, after flashing, the Func encoder counts **down** when turned clockwise, you
have the wrong one. Flash the other; nothing else differs.

### Libraries somewhere else

```bash
make LIB_DIR=/path/to/libs -j8    # expects $LIB_DIR/libDaisy and $LIB_DIR/DaisySP
```

---

## Flashing

The application lives in **internal flash at `0x08000000`**. There is no
bootloader, so the whole 128 KB region is yours.

### With an ST-Link (SWD)

Wire the probe to the Seed's SWD pads, power the module from your rack, then:

```bash
cd firmware && make program
```

Look for `** Verified OK **`.

### Over USB (DFU) — no probe required

Hold **BOOT**, tap **RESET**, release **BOOT**, then:

```bash
dfu-util -a 0 -s 0x08000000:leave -D build/ogham_bytebeat.bin
```

**The `:leave` suffix matters.** Without it the Seed stays parked in DFU mode
with a blank display, looking dead. A trailing
`Error during download get_status` is a benign detach artefact — the write
succeeded.

### ⚠ After flashing, unplug USB before power-cycling

The Seed is powered by USB **and** by the Eurorack bus. With USB connected,
switching your case off does not reset the chip — so the new firmware never
starts and the module looks broken when it is fine. Unplug USB, then power up
from the rack.

---

## Troubleshooting

| Symptom | Cause |
|---|---|
| `No rule to make target '.../stm32h7xx_hal.o'` | Submodules not initialised — `git submodule update --init --recursive` |
| `arm-none-eabi-gcc: command not found` | Toolchain not on `PATH` for *this* shell |
| `make: command not found` (Windows) | Make is a separate install — see above |
| `Target voltage: 0.00V` | Module not powered, or the SWD header is not seated |
| `Target voltage: ~1.8V` | Loose SWD/VTREF contact — reseat the probe |
| `open failed` / device busy | Another tool is holding the probe |
| `No DFU capable USB device available` | Not in DFU mode, or (Windows) no WinUSB driver — see Zadig above |
| `LIBUSB_ERROR_ACCESS` (Linux) | udev rules missing — see above |
| Blank display after DFU | Flashed without `:leave`, so still in DFU mode. Re-flash with it, or power-cycle **with USB unplugged** |
| Encoder counts backwards | Wrong encoder build — flash the other one |
| Builds fine, checksum differs | Different compiler version or unpinned submodules. Harmless unless you are verifying a release |

Anything not covered here, please open an
[issue](https://github.com/stevec64/keeos-ogham/issues).
