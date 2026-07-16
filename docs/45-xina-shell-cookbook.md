# 45 — XINA Shell Cookbook

A cabinet does not become trustworthy when the attract screen appears. It becomes trustworthy when the 1999 operating system underneath it can tell us what it thinks is happening: which tasks are alive, whether CMOS is sane, whether the driver board is present, whether pricing survived, and whether the audio board is really ready. The `%` prompt on COM1 is that truth serum.

This cookbook turns the XINA shell from a spooky service-port relic into a day-zero checklist for Encore. Every normal command below was exercised against a single Star Wars Episode I QEMU boot using Encore serial TCP. The purpose text cross-references the Williams/Gerwiki XINA command reference; the deeper operating-system background lives in [06 — XINA OS Deep Dive](06-xina-os-deep-dive.md), while savedata implications live in [09 — Savedata](09-savedata.md).

> [!IMPORTANT]
> These captures are emulator evidence, not a promise about every real cabinet. Real hardware may have different PDB, Ethernet, RTC, CMOS, or DCS behaviour. Use this cookbook as a scriptable starting point, then validate on wire.

> [!WARNING]
> Destructive or rebooting forms were intentionally deferred: `mon`, `reboot`, `exit`, process termination, CMOS clearing/resetting, firmware loading, and commands that energize real outputs. The cookbook shows safe/no-op/status forms unless explicitly stated.

## Quickstart: attach to COM1 in three lines

```sh
scripts/run-qemu.sh --game swe1 --headless --serial-tcp 4444
rlwrap nc 127.0.0.1 4444
help
```

> [!TIP]
> `--serial-tcp 4444` is the friendly alias for `--uart-tcp 127.0.0.1:4444`; see [02 — Quickstart](02-quickstart.md) and [41 — CLI Keyboard Guide](41-cli-keyboard-guide.md). On a real cabinet the Williams reference describes 9600 baud, 8-N-1 on the serial port.

## Capture notes

* Game/update: Star Wars Episode I as auto-discovered by Encore.
* Invocation shape: `scripts/run-qemu.sh --game swe1 --headless --uart-tcp 127.0.0.1:<port>`.
* Tested commands in this document: 95 safe command forms.
* Deferred commands/forms: `mon`, `reboot`, `exit`, process termination, destructive CMOS/reset/update forms, and live output-driving variants.

> [!NOTE]
> Some commands legitimately print usage only, or accept the command and return to `%` with no body. That is still useful: it proves the parser, command registration, and serial path are alive.

## System & info

### `help`

Print the active command table exposed by this SWE1 XINA build. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% help

Commands are:
   ?                  enter              lampmgr            queue             
   audio              errors             lamp               reboot            
   bitmap             escape             leffmgr            replay            
   bootdata           ether              mem                reslist           
   bpool              eventlog           midas              resources         
   bs                 exit               mon                routes            
   clear              fatal              multi              route             
   cmos_buffer        fb                 netstat            rtc               
   cmos               flags              net                sem               
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!IMPORTANT]
> 🛡 Robustness idea: Make this the day-zero smoke test: boot, ask for help, and fail CI if the command set changes unexpectedly.

### `?`

Alias for `help`; useful when the shell is in operator-memory muscle mode. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% ?

Commands are:
   ?                  enter              lampmgr            queue             
   audio              errors             lamp               reboot            
   bitmap             escape             leffmgr            replay            
   bootdata           ether              mem                reslist           
   bpool              eventlog           midas              resources         
   bs                 exit               mon                routes            
   clear              fatal              multi              route             
   cmos_buffer        fb                 netstat            rtc               
   cmos               flags              net                sem               
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!IMPORTANT]
> 🛡 Robustness idea: Use both `?` and `help` in a serial probe to catch weird keyboard/terminal translation bugs.

### `conf`

Show XINA configuration/status table basics. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% conf

Identification
    Xinu version:     XINA: V1.38 - Fri Sep 27 08:01:00 2024

Table sizes
    Number of processes:   130
    Number of semaphores:  200
    Number of devices:     59

Network information
```

> [!TIP]
> 💡 Cabinet idea: Capture it in every support bundle so cabinet reports always include the firmware personality Encore saw at boot.

### `history`

Display the shell command ring buffer. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% history

94. echo __BEGIN_flags_global__
95. flags global
96. echo __END_flags_global__
97. echo __BEGIN_flags_static__
98. flags static
99. echo __END_flags_static__
100. echo __BEGIN_flip__
101. flip
102. echo __END_flip__
103. echo __BEGIN_fupdate__
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!TIP]
> 💡 Cabinet idea: When a remote operator says “I only typed one thing,” history can prove the exact serial session before savedata is trusted.

### `echo`

Echo text through the shell/log path. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% echo hello-from-encore

hello-from-encore
hello-from-encore
```

> [!TIP]
> 💡 Cabinet idea: Use it as a marker in every automated capture so later parsing can prove exactly which serial script produced the bundle.

### `time`

Read the guest clock. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% time


It is Tuesday,  5 May 2519,  2:05:22 am
```

> [!IMPORTANT]
> 🛡 Robustness idea: Compare this to host time after boot and warn if RTC drift would poison audits, replay windows, or tournament timestamps.

### `rtc dump`

Dump RTC chip registers. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% rtc dump

00: 22 00 05 00 02 00 02 05 05 26 26 02 00 80 00 00
10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
20: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
30: 00 00 20 00 00 00 00 00 00 00 00 00 00 00 00 00
40: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

> [!TIP]
> 💡 Cabinet idea: Build an RTC/NTP sanity probe: sync host time, boot XINA, then verify CMOS clock fields before the first game starts.

### `dipsw`

Show the DIP switch byte. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% dipsw

dip switch value = 0x00
```

> [!TIP]
> 💡 Cabinet idea: Record physical cabinet DIP state in audit snapshots so software config and board reality never silently diverge.

### `gx id`

Read MediaGX identification/configuration data. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% gx id

CX55x0 value: 0x1
```

> [!TIP]
> 💡 Cabinet idea: Use it as a guest-side canary that Encore’s MediaGX emulation still looks like the board the firmware expects.

### `devs`

List XINA device table entries. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% devs

Num  Device   minor   CSR    i-vect.  o-vect.   cntrl blk
--- --------  ----- -------- -------- --------  ---------
 0. CONSOLE     0   00000000 00000000 00000000  00000000
 1. COM1        0   000003f8 00000024 00000024  002fee78
 2. COM2        1   000002f8 00000023 00000023  00301170
 3. DISPLAY     0   00000000 00000000 00000000  00000000
 4. AUDIO       0   00000000 00000000 00000000  00000000
 5. KEYBOARD    0   00000000 00000021 00000021  00000000
 6. TERMINAL    0   00000000 00000000 00000000  00000000
 7. ETHER       0   00000000 00000000 00000000  002fa27c
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!TIP]
> 💡 Cabinet idea: Diff this list across RFM/SWE1 and updates to spot missing UART, printer, or pseudo-terminal devices before they become support ghosts.

### `info`

Accepted by this build but produced no body in the capture. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% info
```

> [!TIP]
> 💡 Cabinet idea: Keep it in the probe list anyway: an update that starts printing here may reveal a newly reachable diagnostics path.

## CMOS & bookkeeping

### `cmos headers`

Dump CMOS header records and saved blocks. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% cmos headers


LM/CMOS Headers

  #   Tag      ID         Data
 ---  -------- ---------- ---------------------------------
   0: 11002488 [neonwarm] (   4) 1101fffc-1101ffff  7  0  0
   1: 110024a0 [ neonrun] (   4) 1101fff8-1101fffb  0  0  0
   2: 110024b8 [BDPwrUpC] (   4) 1101fff4-1101fff7 13  0  0
   3: 110024d0 [BDAudClr] (  28) 1101ffd8-1101fff3  3  8  0
   4: 110024e8 [BDHISTsc] ( 124) 1101ff5c-1101ffd7  a  0  0
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!IMPORTANT]
> 🛡 Robustness idea: Validate Encore savedata by snapshotting headers before/after exit; changed checksums should match the cabinet actions just performed.

### `cmos_buffer headers`

Show flash-CMOS buffer headers. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% cmos_buffer headers


LM/CMOS Headers

  #   Tag      ID         Data
 ---  -------- ---------- ---------------------------------
```

> [!IMPORTANT]
> 🛡 Robustness idea: Use it to catch half-persisted bookkeeping: if buffer headers disagree with CMOS headers, block cabinet handoff and ask for a clean shutdown.

### `clear`

Print usage for the destructive CMOS clear path. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% clear

clear <cmos>
```

> [!TIP]
> 💡 Cabinet idea: Expose only behind a giant confirmation in any future maintenance UI; accidental `clear cmos` is day-zero pain.

### `continue`

Internal script-flow command; with no script context it prints usage and returns. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% continue
```

> [!TIP]
> 💡 Cabinet idea: Generated service scripts can reserve it for future flow control, but day-zero audit scripts should stay linear and obvious.

### `credit info`

Show coin-slot counters, debounce timers, and pulse state. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% credit info

slot total coins debounce deb_timer timer timer_pulse switch
---- ----- ----- -------- --------- ----- ----------- ------
  0      0     0       75         0     0           0 False
  1      0     0       75         0     0           0 False
  2      0     0       75         0     0           0 False
  3      0     0       75         0     0           0 False
  4      0     0       75         0     0           0 False
  5      0     0       75         0     0           0 False
  6      0     0       20         0     0           0 False
  7      0     0       75         0     0           0 False
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!IMPORTANT]
> 🛡 Robustness idea: After wiring a real coin door, pulse each slot and assert exactly one counter moved — no phantom credits, no stuck switch.

### `price_current`

Show current coin values. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% price_current


Currently Running Pricing Table, 2 lines

Coin Value   Credit Value   Fraction Value
   00025          00000          00001     
   00050          00000          00001     
```

> [!TIP]
> 💡 Cabinet idea: Automated price-table validation: boot a new savedata image and prove the live prices match the operator card before opening the cash door.

### `price_dyn`

Show dynamic/multiple-coin pricing state. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% price_dyn

Dynamic is on, count = 1
$0.50 - 1 Credit
```

> [!IMPORTANT]
> 🛡 Robustness idea: Detect oddball regional pricing before launch; Encore can warn if dynamic pricing is enabled in a free-play event profile.

### `price_table`

Dump the price/coin table summary. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% price_table


Current Pricing Table, 2 lines

Coin Value   Credit Value   Fraction Value
   00025          00000          00001     
   00050          00000          00001     
Converted...
   00025          00000          00001     
   00050          00001          00000     
```

> [!TIP]
> 💡 Cabinet idea: Generate a “cabinet pricing receipt” for operators after every update so pricing regressions are visible immediately.

### `printout pricing`

Print the pricing audit report. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% printout pricing


Game Pricing
------------

   Amount           Credits
  ----------      -------------
       $0.25        1/2 Credit
       $0.50          1 Credit
```

> [!TIP]
> 💡 Cabinet idea: Bundle this with `price_current` in a pre-event checklist; humans read the printout, scripts check the raw command.

### `hstd`

Dump high-score-to-date tables. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% hstd

High Score Tables Dump

Highest Scores - Priority: 9000

Members:
Grand Champion - Pri=9000
High Score 1 - Pri=8999
High Score 2 - Pri=8998
High Score 3 - Pri=8997
High Score 4 - Pri=8996
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!TIP]
> 💡 Cabinet idea: Auto-collect high scores before experiments, then restore or publish them to a local web dashboard.

### `replay info`

Show replay/extra-ball target state. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% replay info

replay mode      auto (Extra Ball @ 60,000,000)
target_pct       5
start            60,000,000
levels           1
plays            0
last_block       0
adjustments      0
score            60,000,000
boost            00
total            60,000,000
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!IMPORTANT]
> 🛡 Robustness idea: Validate that updates did not reset replay policy; day-zero cabinets should not surprise players with wrong award thresholds.

### `replay buckets`

Dump replay bucket accounting. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% replay buckets

 99: [     495,000,000 -      499,999,999]    0,  0      0,  0    (0)
 98: [     490,000,000 -      494,999,999]    0,  0      0,  0    (0)
 97: [     485,000,000 -      489,999,999]    0,  0      0,  0    (0)
 96: [     480,000,000 -      484,999,999]    0,  0      0,  0    (0)
 95: [     475,000,000 -      479,999,999]    0,  0      0,  0    (0)
 94: [     470,000,000 -      474,999,999]    0,  0      0,  0    (0)
 93: [     465,000,000 -      469,999,999]    0,  0      0,  0    (0)
 92: [     460,000,000 -      464,999,999]    0,  0      0,  0    (0)
 91: [     455,000,000 -      459,999,999]    0,  0      0,  0    (0)
 90: [     450,000,000 -      454,999,999]    0,  0      0,  0    (0)
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!TIP]
> 💡 Cabinet idea: Trend replay buckets over league nights to tune award generosity without guessing.

### `replay check`

Run the replay consistency check. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% replay check

replay database is: valid
```

> [!IMPORTANT]
> 🛡 Robustness idea: Make it part of an audit-snapshot script before shutdown; catch corrupt replay math while the machine is still powered.

## Switches & lamps

### `switch callbacks`

List switch callback bindings. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% switch callbacks

ID 44 func 0x2c9668 row_bit 0x0 state 0 flags D_OPEN | D_CLOSE (0x43)
ID 43 func 0x2c9668 row_bit 0x0 state 0 flags D_OPEN | D_CLOSE (0x43)
ID 42 func 0x2c9668 row_bit 0x0 state 0 flags D_OPEN | D_CLOSE (0x43)
ID 41 func 0x2c96f8 row_bit 0x0 state 0 flags D_OPEN | D_CLOSE (0x43)
ID 40 func 0x2c96f8 row_bit 0x0 state 0 flags D_OPEN | D_CLOSE (0x43)
ID 81 func 0x1d1e98 row_bit 0x2 state 1 flags D_OPEN | D_CLOSE | D_TILT | D_GO | D_BONUS | D_NT (0x45f)
ID 82 func 0x1d1e84 row_bit 0x0 state 0 flags D_OPEN | D_CLOSE | D_TILT | D_BONUS | D_NT (0x417)
ID 80 func 0x1d1e40 row_bit 0x0 state 0 flags D_CLOSE | D_TILT | D_GO | D_BONUS | D_NT (0x45e)
```

> [!TIP]
> 💡 Cabinet idea: Compare against expected game mode callbacks during a switch-matrix burn-in so dead optos are caught before balls are installed.

### `switch counters`

Dump switch hit counters. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% switch counters

Switch Maintenance Counters:
00:  060 060 060 060 060 060 060 060
08:  060 060 060 060 060 060 060 060
16:  060 060 060 060 060 060 060 060
24:  060 060 060 060 060 060 060 060
32:  060 060 060 060 060 060 060 060
40:  060 060 030 030 030 060 060 060
48:  060 060 060 060 060 060 060 060
56:  000 000 000 000 000 000 000 000
64:  060 000 000 000 000 000 000 000
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!TIP]
> 💡 Cabinet idea: A burn-in script can pulse every emulated/real switch and verify exactly one counter increments per closure.

### `switch timers`

Show switch timing/debounce data. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% switch timers

23: time 4000 skip False prev 0x0 next 0x34b628
22: time 4000 skip False prev 0x34b63c next 0x0
```

> [!IMPORTANT]
> 🛡 Robustness idea: Use it to tune real LPT pacing: jittery switch timers are an early warning of bad bus timing or noisy wiring.

### `lampmgr`

Print lamp-manager usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% lampmgr

Usage: lampmgr
< list[_n] | matrixes[_n] | debug<on|off> | log<on|off> | on | off > 
```

> [!TIP]
> 💡 Cabinet idea: Future cabinet UI can toggle lamp-manager logging only during controlled tests, then turn it off before gameplay.

### `lamp dump lamp`

Dump lamp duty values. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% lamp dump lamp

Lamp Matrix
 A 1 : 00 00 00 00 00 00 00 00
 A 2 : 00 00 00 ff 00 00 00 00
 A 3 : 00 00 00 00 00 00 00 00
 A 4 : 00 00 00 00 00 00 00 00
 A 5 : 00 00 00 00 00 00 00 00
 A 6 : 00 00 00 00 00 00 00 00
 A 7 : 00 00 00 00 00 00 00 00
 A 8 : 00 00 00 00 00 00 00 00

```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!TIP]
> 💡 Cabinet idea: Create an attract-mode lamp baseline and diff it after LPT changes so a single stuck column cannot sneak into a release.

### `flip`

Print flipper-control usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% flip

       disable [all | 0-3] | enable [all | 0-3] |
       player <all | 0-3> | computer <all | 0-3> |
       on <all | 0-3> | off <all | 0-3> | debug>
```

> [!IMPORTANT]
> 🛡 Robustness idea: A service-only flipper inhibit could make headless diagnostics safer when a real cabinet is powered with the glass off.

### `drive list`

List/inspect the generic driver outputs. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% drive list
```

> [!IMPORTANT]
> 🛡 Robustness idea: Use it as the raw-output side of a coil/lamp safety interlock: enumerate first, energize later, never the reverse.

### `pdb`

Report Power Driver Board presence and fuse status. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% pdb

Power Driver Board is present version 0x87 (0x80)
PDB: all fuses are good
```

> [!IMPORTANT]
> 🛡 Robustness idea: This is the real-cabinet green light: refuse “ready” status until PDB version and fuse health are captured cleanly.

### `down`

Inject the coin-door Down button. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% down
```

> [!TIP]
> 💡 Cabinet idea: Script service-menu navigation over serial without touching the cabinet, useful for remote operator walkthroughs.

### `up`

Inject the coin-door Up button. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% up
```

> [!TIP]
> 💡 Cabinet idea: Pair with `down`/`enter` to build deterministic menu macros for factory reset and adjustment audits.

### `enter`

Inject the coin-door Enter button. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% enter
```

> [!TIP]
> 💡 Cabinet idea: Use sparingly in scripted menus: log every selection so a remote macro cannot silently change pricing.

### `escape`

Inject the coin-door Escape button. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% escape
```

> [!TIP]
> 💡 Cabinet idea: Make every automated service macro end with Escape until the menu stack returns to attract mode.

### `start`

Inject the front-door Start button. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% start
```

> [!TIP]
> 💡 Cabinet idea: A day-zero smoke can insert a credit, send Start, and prove the game can leave attract mode without keyboard focus.

## Coin & credit

### `errors`

Show summarized known errors. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% errors

total_size            9216
fatals                1
fatal_buffers         6
fatal_buffer_size     1142
fatal_next            0
fatal_buffer_ptr      0x11000050
non_fatals            1
non_fatal_buffers     8
non_fatal_buffer_size 285
non_fatal_next        1
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!IMPORTANT]
> 🛡 Robustness idea: Display this first in a cabinet health dashboard — operators understand a short error summary faster than raw logs.

### `fatal`

Show the fatal error log. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% fatal

Fatal Errors
------------

  Error database is corrupt.
```

> [!IMPORTANT]
> 🛡 Robustness idea: Archive this before and after hardware experiments; a new fatal entry should automatically fail the experiment.

### `nonfatal`

Show the non-fatal error log. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% nonfatal

Non-Fatal Errors
----------------

  Error database is corrupt.
```

> [!TIP]
> 💡 Cabinet idea: Turn recurring NonFatal output into a Prometheus counter so flaky emulation/pacing changes become graphs, not anecdotes.

### `eventlog stats`

Show event-log counters and buffer capacity. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% eventlog stats

Logged events: 10875 this session, 10875 lifetime
Event buffer holds maximum of 5000 entries
Event types: 91 total registered
```

> [!TIP]
> 💡 Cabinet idea: If event volume spikes after a patch, flag it before it wraps the 5000-entry buffer and hides the first clue.

### `eventlog types`

List registered event classes. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% eventlog types

    1)    10000 : 0012c158 (dump_event_playfield)

    2)     2000 : 0026b4c4 (BaseViewManager::DumpEventDisplay)

    3)     2001 : 0026b508 (BaseViewManager::DumpEventUnDisplay)

    4)     2002 : 0026b550 (BaseViewManager::DumpEventUpdateZVal)

    5)     2004 : 0026b5b4 (BaseViewManager::DumpEventActivate)

```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!TIP]
> 💡 Cabinet idea: Build an allowlist for telemetry export: collect gameplay/audit events, ignore chatty debug classes unless a test enables them.

### `game info`

Show game-state counters and current-player bookkeeping. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% game info

GAME: m_tilt False m_game_over True m_bonus_count False
GAME: m_score_since_serve False m_playfield_valid_count 1
GAME: m_players 1 m_player_up 0 m_collecting_balls False
GAME: m_ball_in_play 1 m_balls_per_game 3 m_tilt_warnings 1
GAME: player 0: score 00 multiplier 00 first_ball_start False ball_time 0
GAME: player 0: score_award_level 0 card_id 0
BTIME: search_seconds 10 sw_shooter_lane 7 total_game_time 0
BTIME: last_shooter_lane_state False last_shooter_lane_edge False
BTIME: last_update_time 0 ball_time_accumulation 0 player_time_accumulation 0
BTIME: next_turnoff 0 last_timer 0 ball_timer_on False
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!IMPORTANT]
> 🛡 Robustness idea: Use it as a post-boot attract/game-over assertion before accepting real coins on day zero.

### `pinevents`

Print game event trigger/debug usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% pinevents

usage: pinevents game <on | off> all
       pinevents game <on | off> game_init
       pinevents game <on | off> add_player
       pinevents game <on | off> cycle_players
       pinevents game <on | off> game_start
       pinevents game <on | off> game_restart_game
       pinevents game <on | off> game_restart_ball
       pinevents game <on | off> ball_start
       pinevents game <on | off> first_ball
       pinevents game <on | off> ball_serve
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!TIP]
> 💡 Cabinet idea: During automated playtests, temporarily enable only score/ball events and collect a clean replay timeline.

## Network & remote

### `ether info`

Dump Ethernet driver statistics. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% ether info

ez[0]:
  ez_pdev           0x2fd64c
  ez_paddr          00:00:00:00:00:00
  ez_bcast          00:00:00:00:00:00
  ez_descr          SMC EtherEZ ISA LAN
  ez_aka            ez
  ez_vendor         0
  ez_type           0
  ez_type_str       
  ez_outq           0
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!TIP]
> 💡 Cabinet idea: On cabinets with ISA Ethernet, this is the first “wire is alive” check before trusting web or tournament features.

### `netstat`

Show network socket/status summary. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% netstat

Proto  RQ   SQ  L. Port    Remote IP    R. Port    State    flags   dev
----- ---- ---- ------- --------------- ------- ----------- -----  -----
```

> [!IMPORTANT]
> 🛡 Robustness idea: Poll it during tournament upload tests to catch stuck sockets before a score disappears into UDP silence.

### `net`

Print network control usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% net

usage: net <start | monitor [mac] <on | off> | nif <N>>
  (e.g. net monitor 0x00 0xe0 0x29 0x0f 0xeb 0xf6 on)
```

> [!TIP]
> 💡 Cabinet idea: Wrap dangerous network toggles in explicit maintenance mode; a fat-fingered `net start` should not happen mid-game.

### `routes`

Show routing table. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% routes

      net            mask           gateway     metric intf  ttl  refs use
--------------- --------------- --------------- ------ ---- ----- ---- ---
```

> [!TIP]
> 💡 Cabinet idea: Save routes with every support bundle; “web server unreachable” is often gateway, mask, or TTL, not game code.

### `route`

Print route configuration usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% route

usage: route add <dest> <mask> <gateway> <metric> <ttl>
       route delete <dest> <mask>
```

> [!IMPORTANT]
> 🛡 Robustness idea: Generate route commands from a validated config file instead of hand-typing IP masks into a 1999 shell.

### `ping`

Print ping usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% ping

usage: ping host [size]
```

> [!IMPORTANT]
> 🛡 Robustness idea: A cabinet installer script can ping gateway and score server, then print one friendly pass/fail line.

### `nslookup`

Accepted but prints no lookup body without arguments. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% nslookup
```

> [!IMPORTANT]
> 🛡 Robustness idea: Probe it when Ethernet lands; DNS failure should downgrade online features, not block local play.

### `httpd list`

List embedded HTTP links. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% httpd list

HTTPD Links:
    /HTTP
    /index.html
    /index.htm
    /p2klogo.gif
There are 4 links registered.
```

> [!TIP]
> 💡 Cabinet idea: Auto-open the local status page after boot and verify the web server still publishes its high-score endpoint.

### `httpd stats`

Show embedded HTTP server counters. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% httpd stats

0 connections 0 not found 0 not implemented
```

> [!TIP]
> 💡 Cabinet idea: Detect remote dashboard scraping bugs: 404/not-implemented counters should stay boring during normal monitoring.

### `ifstat 0`

Attempt interface status query; this capture reported interface 0 illegal. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% ifstat 0

ifstat: illegal interface (0--1)
```

> [!TIP]
> 💡 Cabinet idea: Treat that as a useful negative test in QEMU: no phantom NIC should appear unless Encore explicitly models one.

### `igmp`

Print multicast join/leave usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% igmp

usage: igmp <join | leave> <group>
```

> [!TIP]
> 💡 Cabinet idea: Future linked-cabinet experiments can join a test group only after route and Ethernet probes pass.

## DCS audio

### `dcs`

Print DCS sound command usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% dcs

usage: dcs N [track [pan]]
       dcs <+ | ->
       dcs raw <N>
       dcs quiet [track]
       dcs trkvol <trk vol>
       dcs trkpan <trk pan>
       dcs signals [clear]
       dcs version
       dcs [warm] reset
```

> [!TIP]
> 💡 Cabinet idea: Keep the DCS command family in a sound smoke test: version, quiet, and track controls should never wedge the shell.

### `dcs version`

Ask DCS for its version. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% dcs version

DCS version 1.1
```

> [!TIP]
> 💡 Cabinet idea: Record the audio-board identity in support bundles; mismatched sound flash becomes obvious.

### `audio`

Print audio mixer command usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% audio

usage: audio init
       audio info
       audio quiet {0 - 5 | all}
       audio volmin
       audio voldef
       audio volmax
       audio vol++
       audio vol--
```

> [!TIP]
> 💡 Cabinet idea: Expose volume min/default/max in a cabinet UI without making operators remember shell syntax.

### `audio info`

Show audio subsystem state. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% audio info

trk Q head Q tail code pri prop time left fader
--- ------ ------ ---- --- ---- --------- -----
 0       0      0    0   0 0x00         0 False
 1       0      0    0   0 0x00         0 False
 2       0      0    0   0 0x00         0 False
 3       0      0    0   0 0x00         0 False
 4       0      0    0   0 0x00         0 False
 5       0      0    0   0 0x00         0 False
```

> [!IMPORTANT]
> 🛡 Robustness idea: After boot, verify DCS is enabled before claiming “attract ready”; silent attract mode is a real failure.

### `midas info`

Show MIDAS/cashless-audio-adjacent state. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% midas info

Midas is: disabled
  packets sent:          0
  packets sent again:    0
  good packets rcvd:     0
  bad packets rcvd:      0
  unkn packets rcvd:     0
  unkn sc packets rcvd:  0
  Tx state:              IDLE
  RVI state:             False
  monitor is:            OFF
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!TIP]
> 💡 Cabinet idea: Keep it as a placeholder probe for regional/cashless builds where payment peripherals may talk through MIDAS paths.

## Updates & flash

### `bootdata current`

Show current boot image metadata. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% bootdata current

PRISM FLASH:
Boot data structure:

  boot_data_timestamp: "Fri Oct 31 07:16:34 2025"

  boot_data_checksum:  0x8aa54af5

  boot_data_version:   1.1

  boot_data_size:      8192
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!TIP]
> 💡 Cabinet idea: Before applying any update, snapshot bootdata so rollback knows which bank/image the cabinet actually used.

### `fupdate`

Print firmware-update usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% fupdate

usage: fupdate load <com1 | com2> <baudrate> [sf | sfonly]
       fupdate enable
       fupdate disable
```

> [!IMPORTANT]
> 🛡 Robustness idea: Never automate update load blindly; use this only after savedata, bootdata, and power-health snapshots are stored.

### `updtmgr list`

List scene/background/sound update-manager groups. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% updtmgr list


Update manager contains the following groups: (all ids: 0x000000ff)
 - Group 'shield' has id: 0x00000004 and is NOT currently overridden.
   Preemption during update is disabled.
     Update Object Name   Pri   Flags  OK?
   ---------------------- --- -------- ---
         collecting balls 900      TG.  No
              endball off 900      TG.  No
             nonvalid off 900      TG.  No
            JEDI MB ready 500      TG.  No
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!IMPORTANT]
> 🛡 Robustness idea: Diff update groups between official and community bundles to catch missing attract/music overrides early.

### `pub`

Print public boot/update board dump usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% pub

usage: pub <game | sound1 | sound8> dump <address> [<count>]
```

> [!TIP]
> 💡 Cabinet idea: A future PUB-card verifier could read specific addresses and compare against the update bundle manifest.

## Diagnostics & profiling

### `bitmap info`

Show bitmap allocation summary. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% bitmap info

System RAM allocations:   0 total
Waste region allocations: 0 now (0 max), 0 total
Main region allocations:  5 now (6 max), 6 total
```

> [!IMPORTANT]
> 🛡 Robustness idea: Catch asset leaks by polling after attract loops; a rising bitmap count means a scene path is not releasing memory.

### `bpool`

Run the block-pool diagnostic stub. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% bpool
```

> [!TIP]
> 💡 Cabinet idea: Keep it in broad smoke tests; a once-empty diagnostic gaining output in a new ROM is worth noticing.

### `bs`

Print ball-search/debug usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% bs

usage: bs <debugon | debugoff | off | on>
```

> [!IMPORTANT]
> 🛡 Robustness idea: On real hardware, gate any ball-search debug behind glass-off/coil-safe mode to avoid surprise mechanisms.

### `deffmgr`

Print display-effect-manager usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% deffmgr

Usage: deffmgr < list[_n] | debug<on|off> | log<on|off> | entry <addr> |
    names[_n] | unreq <addr> > 
```

> [!TIP]
> 💡 Cabinet idea: When an effect locks the display, `deffmgr list` can become the operator-friendly “what owns the screen?” tool.

### `dgstat`

Accepted by this build with no body. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% dgstat
```

> [!TIP]
> 💡 Cabinet idea: Use as a compatibility sentinel across game versions; output appearing here may identify display-generator state.

### `dispmgr`

Print display-manager usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% dispmgr

Usage: dispmgr < list[_n] debug<on|off> | entry<addr> | frames | fclear |
    locks[_n] | log<on|off> | on | off >
```

> [!IMPORTANT]
> 🛡 Robustness idea: A frame-lock watchdog can query display-manager frames/locks when attract appears frozen but the CPU is alive.

### `fb`

Print framebuffer adjustment usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% fb

usage: fb <clear | bars | border | pillars | vsyncs | flip | sync V(0:1) H(0:1)>
```

> [!IMPORTANT]
> 🛡 Robustness idea: Provide a safe “glass removed” flip toggle with a timeout so techs can work from the front of the cabinet.

### `flags local`

Dump local flags. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% flags local

LocalFlag::debug() 23 flags
    0: init False carry True
    1: init False carry True
    2: init False carry True
    3: init False carry True
    4: init False carry True
    5: init False carry True
    6: init False carry True
    7: init False carry True
    8: init False carry True
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!TIP]
> 💡 Cabinet idea: Sample flags before/after scripted play to prove mode state returns to idle cleanly.

### `flags global`

Dump global flags. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% flags global

GlobalFlag::debug() 5 flags
```

> [!TIP]
> 💡 Cabinet idea: Use global-flag diffs as a low-cost regression oracle for attract, service, and game-over transitions.

### `flags static`

Dump static flags. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% flags static

StaticFlag::debug() 0 flags
```

> [!TIP]
> 💡 Cabinet idea: Static flag snapshots help detect accidental ROM/update personality changes after a savedata migration.

### `leffmgr`

Print lamp-effect-manager usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% leffmgr

Usage: leffmgr < list[_n] | debug<on|off> | log<on|off> | unreq <addr> |
    names[_n] >
```

> [!IMPORTANT]
> 🛡 Robustness idea: A lamp-effect watchdog could report which effect owns a stuck lamp pattern before the operator power-cycles.

### `multi list`

List multi-device registrations/status. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% multi list

LOCKUP 5: flags 0x00000000 0x2e2d88
LOCKUP 5: switches (32)
LOCKUP 5: m_stable_ball_count 1 m_locked_balls 0 m_locks_enabled 0
LOCKUP 5: m_kickout_retries 10 m_kickout_rest 90
LOCKUP 5: m_suspend_kickout False m_suspend_kickout_waiters 0x0
LOCKUP 5: plyr_ctrl: switch_id 96 lamp_id 128 timeout_adj 0x0
LOCKUP 5: m_plyr_ctrl_released False
LOCKUP 5: m_swd_deferred False m_swd_deferred_dispatch False
LOCKUP 5: m_stable_legal_time 48 m_stable_illegal_time 144
LOCKUP 5: m_kickout_flags 0
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!TIP]
> 💡 Cabinet idea: Great for real driver-board bring-up: prove every expected multi-device is registered before enabling coils.

### `pool stat`

Show pool allocator statistics. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% pool stat

id   size   count  maxused  inuse   total  sem
--- ------ ------- ------- ------- ------- ---
Total pool memory allocated: 0
```

> [!IMPORTANT]
> 🛡 Robustness idea: Track pool high-water marks in soak tests; creeping usage is easier to catch before the fourth hour of a party.

### `dump 0 16`

Dump guest memory bytes. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% dump 0 16

dump:
0x00000000  00 00 00 00 2f 00 31 00 00 00 00 00 00 00 00 00    ..../.1.........
```

> [!TIP]
> 💡 Cabinet idea: Reserve for forensic scripts with fixed addresses; never ask operators to type arbitrary addresses by hand.

## Process & memory

### `mem stat`

Show heap/memory allocator summary. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% mem stat

Memory: 14680064 bytes real memory, 1879960 text, 496536 data, 158960 bss
 initially: 11546584 avail
 presently: 11086872 avail, 327680 stack, 132032 heap
 malloc_no_resource: currently 96637 bytes in 141 blocks
   maximum 96773 bytes and 148 blocks
```

> [!TIP]
> 💡 Cabinet idea: Make it a soak-test metric: reboot only after proving memory stabilizes across attract/game cycles.

### `mem free`

List free-memory blocks. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% mem free

 free list:
   block at 0x00010070, length    24
   block at 0x000100b0, length    32
   block at 0x00018150, length     8
   block at 0x000182c0, length    24
   block at 0x00018330, length    32
   block at 0x000183a0, length     8
   block at 0x00018520, length     8
   block at 0x00018550, length     8
   block at 0x00018620, length     8
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!IMPORTANT]
> 🛡 Robustness idea: Fragmentation drift can be graphed during long tournaments to catch leaks that total-free bytes hide.

### `ps`

List XINU processes, priorities, states, and stacks. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% ps

Highest process count 60 out of 130, currently 42
xpid apid CM R    name    pct  state  pri  stack range   stack length sem msg
---- ---- -- - ---------- --- ------- --- ------------- ------------- --- ---
   0   -1 -- R     prnull  47  ready    0 dfe000-dffffd     184/ 8192  -   -
   1   -1 -- - grimreaper   0  wait   999 df9ffc-dfdff9     148/16384 199  -
   8    5 NP - multi_devi   0 *sleep   20 dd5ffc-dd7ff9     168/ 8192  -   -
   9    6 NP - multi_devi   0 *sleep   20 de7ffc-de9ff9     168/ 8192  -   -
  12    3 NP - multi_devi   0 *sleep   20 dabffc-dadff9     228/ 8192  -   -
  13    3 NP - multi_devi   0 *sleep   20 dafffc-db1ff9     228/ 8192  -   -
  14    3 NP - multi_devi   0 *sleep   20 db1ffc-db3ff9     228/ 8192  -   -
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!TIP]
> 💡 Cabinet idea: This is the scheduler stethoscope: if attract freezes, `ps` tells whether shell, display, update, or lamp tasks are stuck.

### `pty stat`

Print pseudo-terminal status usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% pty stat

No PTYs are in use.
```

> [!TIP]
> 💡 Cabinet idea: When remote serial bridges get added, PTY status can prove whether COM plumbing is alive before blaming XINA.

### `kevents`

Print kernel/event tracing toggles for resources, processes, semaphores, and hooks. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% kevents

usage: kevents resource pid <on | off>
       kevents proc_create <on | off>
       kevents proc_suicide <on | off>
       kevents proc_kill <on | off>
       kevents proc_reap <on | off>
       kevents sem_create <on | off>
       kevents sem_delete <on | off>
       kevents hook_exec <on | off>
```

> [!IMPORTANT]
> 🛡 Robustness idea: A lab-only scheduler trace mode could enable one event class, reproduce a stall, then disable it before normal cabinet play.

### `queue`

Print ready/sleep queue usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% queue

Usage: queue < sleep | ready >
```

> [!IMPORTANT]
> 🛡 Robustness idea: A future watchdog can dump queues when the scheduler looks alive but gameplay tasks never run.

### `resources`

List currently held resources. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% resources

xpid apid    name      type   ref   addr     next     data     size     func
---- ---- ---------- -------- --- -------- -------- -------- -------- --------
  38   -1   leff_run      new   0 000244a0 00024430 000244a0 00000030 00255eac
  38   -1   leff_run      new   0 00024430 00024768 00024430 00000034 00255eac
  38   -1   leff_run      new   0 00024768 00000000 00024768 0000097c 00255eac
  91   -1 DeffAttrKL      new   0 00024250 00023e68 00024250 00000030 00255eac
  91   -1 DeffAttrKL   malloc   0 00023e68 00023e3c 00023e68 000003e8 002559a8
  91   -1 DeffAttrKL   vidmem   0 00023e3c 00023c40 003579c8 000be700 002a7498
  91   -1 DeffAttrKL   malloc   0 00023c40 00023c00 00023c40 000001a0 002559a8
  91   -1 DeffAttrKL   malloc   0 00023c00 00023bd0 00023c00 00000040 002559a8
```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!IMPORTANT]
> 🛡 Robustness idea: Detect startup deadlocks by asserting key resources appear after DCS init and disappear on shutdown paths.

### `reslist`

Dump the resource-manager list. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% reslist

Resource Manager : System list

Storage Type 50 : [0 entries, 0 free, s=0001fdac r=00000000]
                   (32608 bytes free,buffered)

     Res #  ResourceID  Attributes      DTyp  Len  R-Block  Data Tag
------  --------------- ----  ---  -------- --------

Total Length for all shown resources = 0

```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!TIP]
> 💡 Cabinet idea: Store a compressed reslist in support bundles; it is the guest OS equivalent of a cabinet core sample.

### `sem`

Show semaphore status. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% sem

sem count qhead qtail xpid
--- ----- ----- ----- ----
114     1   358   359  119
115    32   360   361  119
151     1   432   433   91
152     1   434   435   91
155     1   440   441   99
156     1   442   443   99
157     1   444   445   99
```

> [!TIP]
> 💡 Cabinet idea: Semaphore snapshots help separate “device not interrupting” from “task waiting on a lock forever.”

### `sleep`

Print script sleep usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% sleep

You gave me 1 args
usage: sleep delay
```

> [!TIP]
> 💡 Cabinet idea: Use it in generated serial scripts to pace service-menu navigation without host-side timing hacks.

### `stack history`

Dump stack history/high-water diagnostics. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output excerpt (the command printed more in the test log):

```text
% stack history

Total freestk() calls (916) - history:

index    ptr    length  adjd_ptr

----- -------- -------- --------

   0  00de1ff8 00002000 00ddfffc

   1  00dd9ff8 00002000 00dd7ffc

```

> [!NOTE]
> Output was longer than the excerpt above; the shown lines are copied verbatim from the capture and trimmed only for readability.

> [!IMPORTANT]
> 🛡 Robustness idea: Turn stack high-water marks into a red/yellow/green health indicator before enabling real coils.

### `timerq`

Accepted by this build with no body. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% timerq

no entries
```

> [!TIP]
> 💡 Cabinet idea: Still worth probing: a future verbose timer queue would be gold for bus-pacing and switch debounce bugs.

### `term`

Print terminal-port mode usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% term

usage: term <on | off | capslock | control | swap>
```

> [!TIP]
> 💡 Cabinet idea: A remote-support wrapper can restore terminal mode (`capslock`, `control`, `swap`) if a previous session made input weird.

### `zombie`

Print zombie-process command usage. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% zombie

use: zombie process-id
```

> [!TIP]
> 💡 Cabinet idea: Use only in lab scripts; a zombie marker could preserve a dying process long enough to inspect its stack.

## Misc

### `pal`

Accepted with no body in this capture. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% pal

pal: not found
```

> [!IMPORTANT]
> 🛡 Robustness idea: Treat as unknown-but-safe-to-probe; if a cabinet build prints PAL state, add it to hardware identity snapshots.

### `vdai info`

Show VDAI bookkeeping status. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% vdai info

vdai: disabled, ok False, state BusyWait, readouts 0, errors 0, aborts 0
```

> [!TIP]
> 💡 Cabinet idea: For route/accounting compliance builds, capture VDAI state before and after updates.

### `zc`

Accepted with no body in this capture. The Williams XINA reference documents this command family; the capture below is the safe form tested in Encore.

Captured output:

```text
% zc

state BAD
```

> [!TIP]
> 💡 Cabinet idea: Keep it in the compatibility matrix as an “unknown command stayed harmless” check.

## Deferred and dangerous commands

### `mon`, `reboot`, `exit`, and `kill <process-id>`

The Williams reference includes reboot/monitor/session-exit/process-control paths. They are real, but they were not exercised in the capture because this cookbook needed one stable boot and because a real cabinet should not restart or terminate tasks during a read-only audit.

```text
% reboot
[deferred: would restart the machine]
% mon
[deferred: would reboot into verbose monitor logging]
% exit
[deferred: exits a running script/session context]
% kill <process-id>
[deferred: would terminate a live XINU process]
```

> [!CAUTION]
> Treat these as lab-only controls. Encore should wrap them in explicit confirmation, savedata snapshots, and a “cabinet is safe” interlock before exposing them to operators.

## Future Encore integration

* `scripts/cabinet-burnin.sh`: boot with `--serial-tcp`, pulse switch rows, read `switch counters`, dump `lamp dump lamp`, and fail on missing increments.
* `scripts/audit-snapshot.sh`: collect `eventlog stats`, `nonfatal`, `fatal`, `cmos headers`, `price_current`, `price_table`, `replay info`, and `hstd` before every shutdown.
* CI smoke test: build QEMU, boot headless, send `help`, and assert the command table still contains the expected XINA set.
* Day-zero health probe: require clean `pdb`, sane `mem stat`, stable `ps`, valid `dcs version`, and boring `errors` before enabling real coils.
* Pricing validator: compare `price_current`, `price_dyn`, `price_table`, and `printout pricing` against an operator-authored YAML profile.
* Event telemetry bridge: stream `eventlog stats/types`, selected `pinevents`, and `nonfatal` counts into Prometheus/Grafana for tournaments.
* RTC guard: compare host time to `time`/`rtc dump`; warn before audits or high-score timestamps drift.
* Update safety wrapper: snapshot `bootdata current`, savedata, and `updtmgr list` before any `fupdate` workflow.
* Remote support bundle: one command that captures shell outputs, QEMU version, savedata hashes, and [41 — keyboard/control settings](41-cli-keyboard-guide.md).

> [!IMPORTANT]
> The win is not just “we can type old commands.” The win is that Encore can turn XINA’s own introspection into repeatable cabinet confidence checks before the first public game.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
