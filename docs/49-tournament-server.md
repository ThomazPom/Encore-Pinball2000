# 49 — Tournament-server preservation research

Pinball 2000 contains a real network-tournament client, usually identified by
its internal `jts_*` symbols. Encore currently preserves enough of the network
hardware to study that client. It retains one unsupported community server as
a laboratory artifact, but does **not** provide a supported tournament service
or an emulated card reader.

> [!IMPORTANT]
> Network tournament play is not a supported Encore feature. The recovered
> Python 2 relay has passed one bounded RFM 1.90 transport test, but it has no
> established licence, complete protocol suite or working card-reader path.
> Enabling the network card does not change that status.

This page records what can be proved from the preserved game programs, current
Encore implementation and contemporary observations. It also defines the
evidence needed before tournament support could be claimed.

## Do not confuse three different features

The guest exposes three related-looking facilities with different owners:

| Facility                       | Where it runs                                            | What it does                                                                                    | Encore status                                   |
| ------------------------------ | -------------------------------------------------------- | ----------------------------------------------------------------------------------------------- | ----------------------------------------------- |
| ordinary/local tournament play | game rules and operator adjustments                      | changes local play or award rules                                                               | part of the guest; not a network server         |
| built-in HTTP service          | XINA inside the guest                                    | publishes machine statistics and identification                                                 | reachable with the supported network transports |
| JTS network tournament         | guest JTS client plus an external server and card reader | identifies players, allocates tournament play, uploads results and retrieves standings/pictures | preservation research only                      |

The **Tournament Play** adjustment does not itself provide JTS. Likewise, a
successful request to the guest's HTTP page proves Ethernet and TCP reachability,
not tournament-server compatibility. See [Optional network
card](48-network.md) for the supported network boundary.

## Evidence in preserved guest programs

The strongest readable symbol table is SWE1 2.10. It names a complete client
surface:

- `jts_send_powerup`, `jts_game_start_check`, `jts_game_start` and
  `jts_game_over`;
- `jts_card_scan`;
- tournament, division and player-information requests;
- player-division and picture-data requests;
- message read/write, header creation, timeout and debug state;
- a `jts` XINA command with individual test operations.

The same image contains operator resources named **Tourney IP Address**,
**Network Login** and **Network Password**, plus COM2 **Card Reader** mode.
The visible response formats include tournament and division names, player ID,
name, score, place and pending state. Picture metadata includes byte size,
dimensions, compression type and palette size.

> [!NOTE]
> Presence is not interoperability proof. Symbols and messages prove that a
> client was compiled into the image; they do not prove that every preserved
> release works with the same server, that a server is still reachable, or
> that the relevant operator path can complete under Encore.

### Version survey

The repository's preserved images were surveyed on 2026-09-26. The following
is an inventory of static evidence, not a compatibility matrix:

| Preserved family | Static result |
|---|---|
| RFM 1.20 | no surveyed tournament-server message set; this is also a pre-network image in Encore's ROM audit |
| RFM 1.30 through 1.60 | JTS request/error strings remain present; each surveyed table exposes 25 names beginning with `jts_` |
| RFM 1.80 through 2.60 | JTS request/error strings remain present; each surveyed table exposes 32 names beginning with `jts_` |
| SWE1 1.30 | tournament UI text is present, but the surveyed symbol table does not expose the later JTS routine set |
| SWE1 1.40 and 1.50 | 25 names beginning with `jts_` |
| SWE1 1.66 through 2.10 | 32 names beginning with `jts_` |

This is why neither the newest version number nor a visible Tournament option
should be used as shorthand for “known working server client.” A real matrix
would require each exact guest image, a fixed server build and recorded packet
exchanges.

### RFM 1.80's memory cost

The myPinballs technical setup separates two thresholds: RFM 1.5 or later can
connect to its tournament system, while scrolling attract-mode scores require
1.8. It also warns that 1.8 needs at least 8 MiB although most cabinets shipped
with only 4 MiB: [myPinballs technical setup](https://mypinballs.com/tournament/core/techsetup.jsp).

The preserved programs confirm that this is a hard guest-memory boundary, not
just conservative installation advice. RFM 1.80's XINU `sizmem()` returns
the raw value `0x800` for 8 MiB; 1.50 and 1.60 return `0x400` for 4 MiB,
and the preserved 1.90-and-later line returns to `0x400`. The full comparison
and implications for original hardware and Encore are in
[Game changelogs](50-game-changelogs.md#why-rfm-180-needs-8-mib).

## Protocol facts established so far

Disassembly of SWE1 2.10 establishes these properties:

- the destination address is the numeric **Tourney IP Address** resource;
- the destination port is `0x0815`, decimal **2069**;
- unicast UDP leaves the preserved XINU stack with TTL 1;
- the 8-byte header holds a 16-bit type, 16-bit transaction identifier and
  32-bit length, all converted to network byte order;
- replies are matched to the expected source address and transaction;
- requests use acknowledgements, retries and timeouts;
- larger data can arrive as continuation packets;
- transfer chunks are bounded at `0x400` bytes.

The request types are recoverable directly from each caller of
`jts_set_header`:

| Type | Request |
|---:|---|
| 1 | game-over result |
| 2 | card scan |
| 3 | main tournament information |
| 4 | division information |
| 5 | players in a division |
| 6 | one player's division |
| 7 | player picture |
| 8 | player information |
| 9 | cabinet power-up |

Type 0 is used by acknowledgement handling, but its payload variants and
negative/continuation encoding still need fixture-backed documentation. The
table deliberately describes requests rather than pretending to specify the
whole wire protocol.

An archived collector page reproduces a contemporary machine trace that saw
the game send UDP from local port 5001 to server port 2069 at boot and game
end. The same page preserves a developer's statement that a Java tournament
server was written for the 1999 Expo project. Treat that page as archived
historical evidence, not as a current download or service guarantee: [Pinball
2000 collectors' tournament notes](https://www.pinball2000.de/pin2000_misc.htm).

Contemporary event reporting independently describes twelve SWE1 machines,
barcode player cards and a central standings server at Expo 1999: [Pinball
Expo 1999 report](https://www.pinballnews.com/shows/expo99/index.html).

Those sources support **UDP/2069** and the original deployment shape. They do
not define every message field. Names inferred from symbols are useful leads;
wire layout must come from disassembly plus captures, not from C++ type names
alone.

### Slirp TTL failure and UDP-stack correction

A controlled RFM 1.80 comparison on 2026-09-27 used the same QEMU 10.0.8,
update, copied savedata, JTS server and mirrored Slirp topology on both sides.
Only the original SMC8416 tournament-TTL compensation differed:

| Run | Duration | Requests at QEMU/Slirp boundary | Server replies | ICMP TTL-expired | IPv4 checksum failures |
|---|---:|---:|---:|---:|---:|
| unmodified parent | 30 s | 5 at TTL 1 | 0 | 5 | 0 |
| compensated build | 65 s | 12 at TTL 2 | 12 | 0 | 0 |

The unmodified packets died inside Slirp before reaching the UDP server. With
compensation, the server logged every type-9 power-on request and every reply
returned through Slirp to the guest capture. A separate 90-second
`--network-auto` run retained the operator-selected destination address and
produced 17 TTL-2 requests, no ICMP TTL-expired response and no bad IPv4
checksum. The transport correction does not by itself establish compatibility
with every historical or community tournament-server protocol.

That first experiment established the routing failure but fixed only JTS.
Inspection of XINU's common `udpsend()` then showed the same default TTL 1 in
every preserved network-capable image. The final correction is therefore a
volatile guest extension, installed only with an actual Slirp peer, that
changes the unicast default to 64 before `netstart`. A 75-second RFM 1.90 run
captured a complete JTS exchange with requests from ephemeral ports
5021–5029 to UDP/2069 at TTL 64; the server replies carried valid UDP checksums
and continued through the type 3, 4, 5, 8 and 7 requests.

The live test also confirmed a separate historical convention. JTS sends from
an ephemeral port but opens its receiving endpoint on local UDP/2069. A normal
`recvfrom()`/`sendto()` relay answers the ephemeral source port, which XINU is
not listening on. Encore therefore retargets Slirp replies sourced by
UDP/2069 to guest UDP/2069 and adjusts their UDP checksum. This rule is
tournament-specific; the TTL extension is stack-wide.

See [Optional network card](48-network.md#slirp-udp-guest-extension) for the
exact activation boundary.

### Surviving implementation leads

These are preservation leads, not Encore dependencies or endorsements:

- The archived developer discussion attributes the original Expo Java server
  to Lyman Sheats. No authenticated source snapshot is preserved here.
- A 2009 report says an enhanced Java tournament package and documentation
  were publicly released through Pinballz.net: [Pinball 2000 news from
  2009](https://www.pinballnews.com/news/p2kupdate.html). Encore has no verified
  copy or provenance record for that package.
- The myPinballs v1.2.2 setup and FAQ pages are still readable as of
  2026-09-26 and describe an independent RFM service, including card-reader
  operation: [game setup](https://www.mypinballs.com/tournament/core/gamesetup.jsp)
  and [technical setup](https://mypinballs.com/tournament/core/techsetup.jsp).
  Website availability does not prove that its tournament endpoint currently
  accepts a game, and Encore has not tested it.
- The Nucore project demonstrated a different tournament system and announced
  an intended open-source server in 2010: [Nucore tournament
  presentation](https://www.pinballnews.com/shows/expo2010/index4.html). It is
  not evidence for the original JTS server and no corresponding source tree
  is part of Encore.
- A recovered 2013 Python 2 “Pinbox Tournament Server” is now retained as a
  [legacy lab tool](../tools/tournament-server-legacy/README.md). Its source
  header attributes `MasterGeek`, but no upstream URL or licence was recovered.
  It is useful evidence for message shapes, not evidence for the original Expo
  Java server.

Do not build a compatibility claim by combining details from these separate
systems. Recover a specific artifact, hash it, establish its license, then test
it against a named guest version.

## What Encore provides today

With a network-capable update, Encore can expose the guest's SMC8416T-compatible
card through isolated user networking, NAT, automatic NAT, passt, mirror or an
existing bridge. The guest sees I/O base `0x300`, IRQ 7 and MAC address
`00:00:c0:01:02:03`.

That gives the historical client a possible packet path. Encore now preserves
one manually launched community JTS relay under `tools/`, but does not make it
a supported service. No current launcher option:

- starts a tournament server;
- decodes or records JTS messages as structured data;
- translates a modern service into the historical protocol;
- configures the guest's Tourney IP, login or password;
- emulates the barcode reader.

The legacy relay can synthesize divisions, players, rankings and pictures from
an HTTP score API. Its recovered picture fixtures are not vendored because no
licence was found, and it has no server conformance suite. The 2026-09-27 test
proves only the documented RFM 1.90 path.

> [!TIP]
> JTS is guest-initiated. The tested Slirp path requires no public
> host-to-guest port forward. That result does not establish equivalent
> behavior for passt or a direct bridge; each transport still needs its own
> captured request/reply smoke.

## The independent COM2 blocker

The tournament UI expects a card reader on COM2. Encore installs a separate
probe-compatible 16550 register model at `0x2f8`, but its host data path is
limited:

- non-loopback COM2 transmit bytes are discarded;
- COM2 has no receive ring or QEMU chardev;
- only COM1 consumes `P2K_UART_INPUT` or a serial chardev;
- only COM1 drives the currently connected UART IRQ4 path.

COM2 can therefore satisfy simple register probing, but a host cannot swipe a
barcode into the guest. A reachable JTS server would still leave normal player
identification incomplete.

This should be implemented as a separate device boundary, not hidden inside a
tournament server. A useful COM2/card-reader feature needs at least:

1. an explicit launcher option and dedicated host backend;
2. byte framing and line settings verified against the guest's card-reader
   initialization;
3. the correct ISA interrupt line and 16550 receive semantics;
4. bounded buffering and visible overflow diagnostics;
5. fixtures for valid, malformed and repeated scans;
6. no accidental reuse of the privileged COM1 XINA console.

The surviving myPinballs hardware instructions describe a tested reader as
RS-232, 8 data bits, no parity and carriage-return termination. Those values
are a starting fixture, not proof that every historical reader or guest
version used identical electrical and framing settings.

## Safe research setup

Do not point a cabinet or emulator at a random historical hostname. The client
can send player identifiers, scores and configured credentials to the selected
numeric address. The retained relay also contacts a clear-text HTTP API and
binds UDP on all host interfaces, so use it only in a controlled lab.

Read the [legacy server README](../tools/tournament-server-legacy/README.md),
then start it explicitly; the Encore launcher never does this:

```bash
cd tools/tournament-server-legacy
P2K_TOURNAMENT_API=http://127.0.0.1:18081/ python2.7 ./server.py
```

The API in this example is a fixture supplied by the researcher, not an
Encore service. Without an API, the relay can still answer the basic power-on
exchange but normally has no player list. The unlicensed `.i64` picture files
are not included.

Use an isolated lab with disposable savedata:

```bash
./scripts/run-qemu.sh \
  --game swe1 \
  --update 2.10 \
  --network \
  --setip 10.0.2.15 255.255.255.0 10.0.2.2 \
  --no-savedata \
  --headless
```

This command only supplies an isolated guest network and known XINA address.
It does **not** start JTS or configure the Tourney IP. Set the guest's numeric
Tourney IP to the controlled server endpoint, save, then reboot rather than
calling `net start` twice. Record the server hash, bind address, API fixture,
guest settings and packet capture alongside every result.

> [!CAUTION]
> Do not use `net start` again after changing guest settings. XINA creates
> duplicate network process sets rather than reconfiguring the existing one;
> that path has caused a real interrupt-stack overflow. Save settings and
> reboot instead.

For protocol work, prefer a private namespace, VM or disconnected lab bridge.
Capture only traffic generated by machines and software you are authorized to
test. Redact login values and player data before publishing artifacts.

## Preservation plan

A defensible implementation should progress in evidence order.

### 1. Preserve the client baseline

For every studied release, retain:

- the exact update component hashes;
- symbol-table output where available;
- the selected network and tournament adjustments;
- game ID, version, language and savedata policy;
- raw serial log and packet capture timestamps.

The 2026-09-26 reference hashes used for the detailed inspection are:

```text
SWE1 2.10 game     36bf84f10bc2410ab7ec36bcafc9a65d0f7963f82bfcfe8592dc1d32917dc62d
SWE1 2.10 symbols  0e83fc2b470bd88c407ad82fc2201b6288bc061355391ebbffd921ea22299744
RFM  2.60 game     9bae270dc68f015dbcc391783b6e703d1ffc312d8c2c635bf00b198335ce4a33
RFM  2.60 symbols  b9da18e9cde74f07fcfb29cd53556ad74565debb6953073f4ff0e9ebae4c3990
```

### 2. Build a passive decoder first

Define message framing from the guest code and verified captures. A decoder
must reject truncated lengths, oversized continuations, unexpected source
addresses, transaction mismatches and unknown message types without inventing
meaning. Keep raw bytes beside decoded output so later corrections remain
auditable.

### 3. Add a deterministic local mock

Only after the decoder has fixtures should a local mock reply. Start with the
smallest exchange, likely power-up and its ACK, then add one operation at a
time. Use fixed synthetic players, divisions, scores and tiny pictures. No
Internet dependency, account system or mutable production database belongs in
the first compatibility target.

### 4. Emulate the card reader separately

Complete and test the COM2 receive/IRQ path before calling the player workflow
functional. A test-only injected scan is useful, but it must traverse the same
guest-visible UART behavior as a future physical or host reader backend.

### 5. Validate complete state transitions

The final suite must cover at least:

- power-up with server present, absent and late;
- accepted, rejected, unknown and repeated card scans;
- game start and game over, including retransmission;
- score submission without duplicate accounting;
- division/player lists and multi-packet picture retrieval;
- malformed, reordered, duplicated and timed-out replies;
- guest reboot, server restart and interrupted exchange;
- both SWE1 and RFM exact versions claimed as compatible.

## Conditions for claiming support

Tournament support remains experimental until all of these are true:

- a redistributable, versioned server or clean-room mock is in the repository
  with provenance and license recorded;
- its protocol fields are backed by disassembly and/or authorized captures;
- a working COM2 card-reader path exists;
- at least one full synthetic player flow completes from scan through score
  and standings on current Encore;
- packet fixtures exercise retries, continuations and hostile lengths;
- the run is repeatable from a documented clean state;
- credentials and player data have an explicit local-only safety policy;
- claims name the exact game updates, Encore commit and transport tested.

Anything less can still be valuable preservation work, but should be labelled
as a decoder, mock-server experiment or partial client trace—not a restored
tournament system.

The current implementation gaps are also tracked in [Known
limitations](35-known-limitations.md), and project-level completion criteria
belong in the [Roadmap](36-roadmap.md).

---

Previous: [Optional network card](48-network.md) · Index:
[Documentation](README.md) · Next: [Community updates](47-community-updates.md)
