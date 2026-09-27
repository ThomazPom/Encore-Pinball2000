# 48 — Optional network card

Encore can add an SMC8416T-compatible ISA Ethernet card to Pinball 2000. The
card is **off by default**. Enable it only for a network-capable update and
choose how its guest traffic reaches the host or LAN.

For most desktop and cabinet installations, start with automatic user-mode
NAT:

```bash
./scripts/run-qemu.sh \
  --game swe1 \
  --update latest \
  --network-auto \
  --setip 10.0.2.15 255.255.255.0 10.0.2.2 \
  --http-port 8080
```

Then open <http://127.0.0.1:8080/>. `--http-port` publishes only the guest's
TCP port 80 and binds it to the host loopback address.

> [!IMPORTANT]
> The network card and transport do not start networking in an old guest that
> has no network stack. Use a network-capable update. Encore's extension audit
> currently recognizes 24 preserved update images and classifies SWE1 1.30
> and RFM 1.20 as pre-network images.

## Choose a transport

| Launcher option | Host privilege | Guest addressing | Reachability | Status |
|---|---:|---|---|---|
| `--network-auto` | none | any working static IPv4 configuration | outbound NAT; optional TCP forwards | recommended rootless mode |
| `--network-nat` | none | normally `10.0.2.15/24`, gateway `10.0.2.2` | outbound NAT; optional TCP forwards | conventional and predictable |
| `--network` | none | normally the same `10.0.2.0/24` values | isolated QEMU user network | useful for contained tests |
| `--network-passt` | none | match the host's usable IPv4 topology | host-socket translation; optional TCP forwards | supported, requires `passt` |
| `--network-mirror` | none | match the host IPv4 subnet | rootless Slirp on that subnet | experimental |
| `--network-bridge NAME` | TAP setup only | configure for the attached LAN | direct Layer-2 LAN attachment | advanced and LAN-exposed |

All modes present the same guest-visible SMC8416T-compatible card. They differ
only in the QEMU network backend and in how host traffic is routed.

> [!TIP]
> `--network-auto` is the least fragile choice when old savedata already
> contains an unknown but valid guest IP. It adapts forwarding to the address
> XINA actually uses instead of requiring that address to match the host LAN
> or QEMU's conventional `10.0.2.0/24` subnet.

## Configure XINA before its first network start

Pinball 2000 stores three persistent IPv4 resources: address, netmask and
gateway. Supply all three with `--setip`:

```bash
./scripts/run-qemu.sh \
  --game rfm \
  --update latest \
  --network-auto \
  --setip 10.23.4.15 255.255.255.0 10.23.4.1
```

`--setip` enables the volatile guest extension, validates the three IPv4
strings, and writes the normal XINA resources immediately before the first
native `netstart`. It does not edit an update ROM. When blank CMOS triggers
the guest's own factory reset later in that boot, a one-shot wrapper reapplies
the same values after the reset.

Only address, mask and gateway are changed. DNS and application-server names
remain guest settings.

With ordinary savedata, those resource writes persist. With `--no-savedata`,
they affect only that disposable run. See [Persistent cabinet
state](09-savedata.md) for the complete state boundary.

The same extension adds this serial-shell command:

```text
setip <address> <mask> <gateway>
```

That command stores new values, but an already running network stack keeps its
old configuration. Reboot the guest to apply the change safely.

> [!CAUTION]
> Do **not** issue `net start` a second time. XINA creates another complete set
> of network processes instead of stopping or reconfiguring the first one. A
> deliberately repeated start has produced duplicate HTTP, Telnet, TCP/IP and
> timer processes and a real interrupt-stack overflow. Save the new settings
> and reboot.

## Automatic mode

`--network-auto` uses QEMU's rootless user network but does not assume the
guest is `10.0.2.15`. The emulated card provides proxy ARP for the guest's
outbound traffic. Encore learns the active source address from an IPv4 or ARP
packet; if no packet has exposed it when the XINA prompt appears, the emulated
UART asks `ifstat 1` once and parses the reported address.

The address-discovery path does not read guest RAM or rewrite IP addresses.
The packet keeps XINA's source and destination addresses; proxy ARP only
steers its Ethernet frame into Slirp. The narrow tournament-TTL compensation
described below is independent of discovery.

TCP forwards are initially unbound from a guest address. After discovery, the
SMC device retargets them to the active XINA IP and reports:

```text
p2k-smc8416: automatic forwards now target XINA 10.23.4.15
```

Automatic mode therefore corrects the transport boundary; it does not rewrite
the guest configuration continuously. XINA still needs a valid address, mask
and gateway and must start its own stack. `--setip` is a convenient way to
guarantee that state on supported updates.

The automatic device holds at most 16 TCP-forward definitions. Count
`--http-port`, `--forward-local` and `--forward` entries together when building
a large service map.

### Tournament TTL compensation on Slirp

The preserved JTS client sends IPv4 UDP requests from port 5001 to destination
port 2069 with TTL 1. That is correct for its original same-LAN tournament
server. Slirp is implemented as a router, however, so its synthetic hop would
consume the only TTL and return ICMP `time exceeded` before NAT could send the
request.

Encore compensates at the emulated-card/Slirp boundary. For an IPv4 first
fragment that is UDP to destination port 2069 with TTL exactly 1, the card
changes the TTL to 2 and recomputes the IPv4 header checksum immediately before
handing the frame to Slirp. Slirp consumes that one added hop, leaving the
original effective TTL of 1 on the host side. Source and destination addresses,
ports, payload and UDP checksum are untouched.

The card tests its connected backend rather than a launcher-option name. The
compensation therefore applies to every Slirp-backed mode (`--network`,
`--network-nat`, `--network-auto` and `--network-mirror`) and cannot activate
for passt or a bridged/TAP connection. The Tourney IP remains entirely under
operator control; it is not required to be `10.0.2.2` or another Encore-owned
address.

## Conventional NAT and isolation

Use conventional NAT when the guest configuration is known:

```bash
./scripts/run-qemu.sh \
  --game swe1 \
  --update latest \
  --network-nat \
  --setip 10.0.2.15 255.255.255.0 10.0.2.2
```

This creates QEMU's `10.0.2.0/24` user network. XINA can initiate traffic to
the host network and Internet through Slirp. Static host forwards target
`10.0.2.15`, so saved guest settings must agree or be replaced with `--setip`.

Use plain `--network` for a contained backend:

```bash
./scripts/run-qemu.sh --game swe1 --update latest --network \
  --setip 10.0.2.15 255.255.255.0 10.0.2.2
```

It selects the restricted QEMU user network and gives the guest no ordinary
outside access. Adding a general forward selects connected user-mode NAT;
choose the transport explicitly when the distinction matters.

`--http-port` is the narrow exception: it implies the card and adds one
loopback-to-guest HTTP forward while preserving the restricted backend. A
2026-09-26 smoke fetched the real guest page through that exact path.

## Passt and host-subnet mirror

`--network-passt` starts an unprivileged, IPv4-only, one-shot `passt` process
and connects QEMU through its private Unix socket. Configure XINA for the
host's usable IPv4 topology:

```bash
./scripts/run-qemu.sh \
  --game swe1 \
  --network-passt \
  --setip 192.168.1.26 255.255.255.0 192.168.1.1 \
  --http-port 8080
```

The runtime preflight installs or requests `passt` when this mode is selected.
No TAP, bridge or firewall rewrite is created by Encore. The daemon is
one-shot; its private socket and temporary directory disappear with the QEMU
run.

`passt` prints a DHCP-style topology summary, but the validated XINA path uses
its persistent static resources rather than acquiring a lease. Configure the
address, mask and gateway that passt reports; the guided installer detects and
proposes the host values.

`--network-mirror` derives the first IPv4 default route, host address and
prefix using `ip`. It configures Slirp with that network and gateway and
enables proxy ARP for XINA. The guest address, mask and gateway must match the
detected host topology; the cabinet installer proposes those values.

> [!WARNING]
> Mirror mode is experimental. A host address or route change can make saved
> guest settings stale. Prefer automatic mode unless matching the host subnet
> is itself the experiment.

## Publish guest TCP services

The forwarding options accept TCP only:

```bash
# Safest: host-local HTTP
./scripts/run-qemu.sh --network-auto --http-port 8080

# Any guest TCP service, host-local only
./scripts/run-qemu.sh --network-auto --forward-local 2323:23

# Bind on every host interface
./scripts/run-qemu.sh --network-auto --forward 8080:80
```

The left port is on the host; the right port is inside Pinball 2000. Options
are repeatable, but each host port must be unique.

`--expose-services` is shorthand for guest HTTP port 80 on host port 8080. It
binds on every host interface and deliberately excludes Telnet. Publish
Telnet, if truly needed, with an explicit `--forward` or `--forward-local`.

> [!WARNING]
> XINA's HTTP, Telnet and other historical services were not designed as
> modern Internet-facing services. Prefer `--http-port` or `--forward-local`.
> Use `--forward`, `--expose-services` or a bridge only on a trusted network
> and only for services you intend to expose.

Forwarding is incompatible with bridge mode. Host-port availability is
ultimately checked by Slirp or passt when the backend starts.

## Direct bridge attachment

`--network-bridge NAME` attaches the SMC card to an existing Linux bridge
through the managed TAP `encore-p2k0`. Encore does **not** create or configure
the bridge itself.

The runtime preparation phase:

1. verifies that `NAME` is an existing Linux bridge;
2. creates `encore-p2k0` for the runtime user when absent;
3. marks the TAP as Encore-owned;
4. attaches it to the requested bridge and brings it up;
5. launches QEMU as the unprivileged runtime user.

The cabinet installer creates a small system service to restore that TAP at
boot. The uninstaller removes it only when its ownership marker matches; it
refuses to repurpose or delete an unrelated interface with the same name.

Bridge mode cannot be combined with NAT, passt, forwarding or `--http-port`.
The guest is directly reachable according to the bridge and LAN policy, so
configure XINA for that LAN and apply host-side filtering outside Encore.

> [!IMPORTANT]
> The bridge lifecycle has implementation and installer coverage, but the
> current project evidence does not certify a real LAN or physical cabinet.
> Treat the first deployment as a controlled network test. The guided path is in
> [Cabinet installation](01-cabinet-installation.md).

## Guest-visible hardware

The optional device combines QEMU's DP8390 packet engine with the small
WD/SMC front end that XINA expects:

| Resource | Guest-visible value |
|---|---|
| ASIC registers | I/O `0x300..0x30f` |
| DP8390 registers | I/O `0x310..0x31f` |
| Interrupt | ISA IRQ 7 |
| shared packet RAM | `0x000d0000..0x000d1fff` (8 KiB) |
| default MAC | `00:00:c0:01:02:03` |
| family byte | `0x2a` |

The machine also installs an eight-byte read-only LAN-ROM shadow at
`0x000d0008..0x000d000f` so XINA can validate the MAC, family byte and
checksum as it would after the original BIOS POST. The shadow exists as a
machine compatibility surface; the complete packet-RAM device appears only
when a network mode adds `p2k-smc8416`.

The experimental Prism Update Board also decodes `0x000d0000`. The launcher
therefore rejects `--pub-card` together with any network mode instead of
creating an ambiguous mapping. See [Memory and I/O map](13-memory-map.md).

## Update and extension coverage

The network adapter itself is independent of game version. Useful networking
also requires a guest image containing the XINA network stack. Automatic
startup IP injection additionally requires the structural guest-extension
ABI.

Check every preserved update without booting it:

```bash
python3 guest-extensions/check-romset.py
```

As of 2026-09-26, the repository inventory reports:

```text
24 supported, 2 pre-network, 0 failed
```

The check proves that required code shapes and the factory-reset target are
present. It does not prove packet transfer, service behavior or safety on a
physical LAN. The broader version boundary is in [Compatibility and
support](30-compatibility-support.md).

## Validation snapshot — 2026-09-26

Bounded emulator smokes booted SWE1 2.10, detected:

```text
ez0: port 0x300 irq 7 mac 00:00:c0:01:02:03 type SMC8416T (8 bit)
```

and fetched the guest's real 1,792-byte `Pin2000 HTTP Server` page through
localhost in each tested transport:

| Transport | Guest configuration used | Result |
|---|---|---|
| automatic NAT | deliberately unrelated `10.77.1.23/24` | IP learned, forward retargeted, HTTP answered |
| conventional NAT | `10.0.2.15/24`, gateway `10.0.2.2` | HTTP answered |
| restricted backend + local HTTP | `10.0.2.15/24`, gateway `10.0.2.2` | HTTP answered; outbound policy not tested |
| mirror | current host subnet/address/gateway | HTTP answered |
| passt | current host subnet/address/gateway | HTTP answered |

Both maintained QEMU builds, 10.0.8 and 10.2.4, register the same SMC device
properties. A separate passt run with fresh state and no static configuration
did not reach HTTP even though passt advertised a DHCP assignment; configuring
the reported host topology did. These were short, headless, software-only
smokes. They do not certify sustained traffic, hostile inputs, the isolated
backend's outbound policy, a real bridge, multi-machine play or a physical
cabinet.

## Troubleshooting

Start with verbose serial output:

```bash
./scripts/run-qemu.sh \
  --game swe1 \
  --update latest \
  --network-auto \
  --setip 10.0.2.15 255.255.255.0 10.0.2.2 \
  --http-port 8080 \
  -v 2>&1 | tee encore-network.log
```

Useful milestones are:

```text
guest extension installed
ez0: port 0x300 irq 7 ... type SMC8416T
querying XINA's active IP through XUART
automatic forwards now target XINA ...
```

- If `ez0` never appears, confirm that a network option was supplied and use a
  network-capable update.
- If Encore reports no compatible guest-extension image, do not assume
  `--setip` was applied; run the ROM-set checker and select a supported update.
- If automatic forwarding never gets a target, confirm that XINA reached its
  prompt and has a nonzero active address.
- If conventional NAT cannot reach the guest service, verify that XINA is
  actually `10.0.2.15/24` with gateway `10.0.2.2`.
- If a forward will not bind, choose an unused host port and check whether it
  is already listening with `ss -ltn`.
- If passt preparation fails, rerun the exact command with `--preflight` and
  install the requested runtime package.
- If bridge preparation fails, verify the existing bridge and the managed TAP
  service; do not create or rename host interfaces merely to bypass Encore's
  ownership checks.
- After changing IP resources from the serial shell, reboot; never use a
  second `net start` as a reconfiguration shortcut.

The full option inventory is in [Command-line reference](03-cli-reference.md),
general failures are covered by [Troubleshooting](04-troubleshooting.md), and
the remaining validation gaps are explicit in [Known
limitations](35-known-limitations.md).

---

Previous: [Roadmap](36-roadmap.md) · Index: [Documentation](README.md) · Next:
[Tournament-server research](49-tournament-server.md)
