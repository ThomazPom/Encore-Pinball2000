# 45 — Official Update Manager Background

This is purely historical background on how Williams shipped Pinball
2000 software updates and how those distribution archives map onto the
extracted `.rom` directories Encore consumes.

## 1. What it is

`Pin2000_UpMgr_130.exe` is the proprietary Windows utility Williams
distributed with each software update for Revenge From Mars and Star
Wars Episode I.

It is a self-extracting installer that unpacks a directory tree of
`.rom` files into a subdirectory named after the update:
`pin2000_<game-id>_<version>_<date>_B_10000000/`.

Inside that directory: `bootdata.rom`, `im_flsh0.rom`, `game.rom`,
`symbols.rom`, `pubboot.rom`, `sf.rom`. These six files together
constitute a complete software update for one game title and version.

## 2. The GUI wrapper and `fupdate.exe`

The Update Manager GUI is a 16-bit Windows application and refuses
to run on 64-bit Windows.

The actual update transmission is performed by `fupdate.exe`, a
Win32 command-line tool included in the package. This binary runs
natively on 64-bit Windows.

`fupdate` sends all six ROM components sequentially over a serial
connection (RS-232, COM1 or COM2) at up to 115 200 baud to the
cabinet. The cabinet's update receiver processes them in order:

```
bootdata → im_flsh0 → game → symbols → pubboot → sf
```

A typical RFM update is roughly 4.75 MB total, taking 10–20 minutes
at 115 200 baud.

## 3. How Encore's bundled updates were obtained

The update `.exe` files are ZIP archives. Extracting the ZIP with
any standard unzip tool yields the `pin2000_*_B_10000000/` directory
tree without needing to run `fupdate` or the Update Manager GUI at
all.

The extracted directory is what Encore reads directly via
`--update DIR`. No `.exe` is ever executed by Encore.

Bundle directories under Encore's `updates/` folder were obtained
this way from the original Williams update archives.

## 4. Pointer to `tools/build_update_bin.py`

If you need to create a flat `.bin` image suitable for flashing to a
real cabinet (bypassing the serial Update Manager),
`tools/build_update_bin.py` concatenates the six ROM components in
the correct order. See
[30-tools-build-update-bin.md](30-tools-build-update-bin.md) for
usage.

This is only relevant for real-hardware flashing — Encore reads the
extracted tree directly and does not need a flat `.bin`.

## 5. Alternative: `fupdate.exe` for modern Windows

On 64-bit Windows, the `fupdate.exe` command-line tool can be run
natively.

1. Connect a USB-to-RS232 adapter and a DB9 null-modem cable from
   the PC to the cabinet's serial port.
2. Force the USB-to-RS232 adapter to COM1 or COM2 in Device
   Manager — the tool only recognises these two port names.
3. Invoke as:

   ```
   fupdate -p\pin2000_50070_0260_..._B_10000000\50070
           -fpin2000_50070_0260_ COM1
   ```

## 6. Alternative: VirtualBox + Windows XP (GUI method)

If `fupdate` is unavailable or the GUI is preferred, install
Windows XP in VirtualBox, configure USB passthrough for the
RS-232 adapter, and run the full Update Manager GUI. This is the
more complex method but uses the original Williams UI.

## 7. Where to find update files

Original Williams updates were historically hosted at
`http://www.planetarypinball.com/mm5/Williams/tech/pin2000/`. This
site may no longer be active; check archive services.

Community-sourced updates (from the mypinballs.com project) may also
be available; these are not official Williams releases.
