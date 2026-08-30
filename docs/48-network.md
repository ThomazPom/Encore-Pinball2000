# 48 — Optional network card

Encore can expose the optional Ethernet hardware expected by the original
Pinball 2000 software:

```sh
scripts/run-qemu.sh --network
```

This adds an SMC8416T-compatible ISA card at I/O `0x300`, IRQ 7, with its
8 KiB shared-memory window at `0xD0000`. XINA uses its original network driver;
Encore does not patch the guest or inject an IP address.

The virtual network is deliberately isolated. It uses QEMU user networking
with restricted outbound access, so enabling the card does not expose the
machine or the old guest network stack to the LAN.

## Guest settings

Configure these values through the game's normal adjustments:

| Adjustment | Value |
|---|---|
| IP Address | `10.0.2.15` |
| IP Mask | `255.255.255.0` |
| Gateway | `10.0.2.2` |
| HTTP Server | `Yes` when HTTP access is wanted |

Restart the game after changing its network adjustments. XINA registers
`netstart()` as a power-up hook: on the next boot it reads the saved IP address,
mask, and gateway, then initializes the interface, routes, ARP, IP, TCP, and the
enabled network daemons. It only performs that initialization when the Ethernet
device is present and the configured IP address is non-zero.

Changing an adjustment does not restart the active network stack. The network
resources have no change hook calling `netstart()`, so their new values normally
take effect at the next XINA boot. During development, the XINA console command
`net start` can invoke the same initialization without a full reboot.

## Local HTTP access

The original game contains a small HTTP server. To expose it only on the host:

```sh
scripts/run-qemu.sh --http-port 8080
```

Then open <http://127.0.0.1:8080/>. This option implies `--network` and forwards
that localhost port to `10.0.2.15:80` inside the isolated network. The host
listener is never bound to the LAN.

The path has been validated with XINA's original driver and server: the guest
recognizes the SMC8416T, exchanges packets with the virtual gateway, and serves
the Pinball 2000 page over the localhost forwarding rule.

Network support remains optional. Encore does not assume that historical
external Pinball 2000 services still exist.

## User-mode NAT and LAN port publishing

For outbound network access without Docker, a TAP, root privileges, or host
firewall changes, use QEMU/libslirp NAT:

```sh
scripts/run-qemu.sh --network-nat
```

The guest settings remain `10.0.2.15`, `255.255.255.0`, and gateway
`10.0.2.2`. To publish selected TCP services on every host interface, add one
or more explicit mappings:

```sh
scripts/run-qemu.sh --network-nat --forward 8080:80 --forward 2323:23
```

Other LAN machines can then reach the guest HTTP and Telnet services through
the host's address on TCP ports 8080 and 2323. Each mapping is deliberately
explicit because it exposes the historical guest service to the host network.

The convenience preset publishes only the built-in HTTP service:

```sh
scripts/run-qemu.sh --expose-services
```

This is equivalent to `--network-nat --forward 8080:80`. Telnet is deliberately
excluded because it is an optional historical administration service; add
`--forward 2323:23` only when it is intentionally enabled in the guest. The
tournament client needs outbound access, which NAT already provides, and does
not need an inbound forwarding rule.

## Configuration-independent NAT

The experimental automatic mode keeps libslirp on its unchanged default
network while allowing XINA to retain any static IPv4 configuration:

```sh
scripts/run-qemu.sh --network-auto
```

The emulated SMC8416 answers every IPv4 ARP request with libslirp's default
gateway MAC. XINA therefore sends every routed Ethernet frame into Slirp while
the enclosed IP packet retains its original source and destination. Libslirp
accepts that source address and ARPs it directly when returning traffic.

Encore does not read or rewrite XINA's IP address, mask, gateway, IP headers or
checksums. Changing the configuration in the service menu therefore does not
restart QEMU or Slirp, but XINA also does not reconfigure its active stack at
that moment. The saved values normally become active when `netstart()` runs at
the next XINA boot, or when `net start` is issued manually on its console.

For inbound forwarding, Encore waits for XINA's console prompt and issues the
read-only `ifstat 1` command through the emulated XUART. Its reply contains the
address of the live Ethernet interface initialized by `netstart()`. Encore uses
that address to retarget the attached Slirp forwards through libslirp's public
API. It does not inspect guest RAM, use update symbols, invoke `net start`,
rewrite packets, or recalculate checksums.

The SMC also learns the active address from the sender field of an outgoing ARP
request or an outgoing IPv4 packet. This provides a natural fallback and lets a
later manual `net start` update forwarding. Until XINA has an active interface,
no inbound forward is installed.

## Mirrored host topology with libslirp

The experimental mirror mode keeps QEMU's built-in, unprivileged NAT but gives
its virtual network the same IPv4 subnet and gateway as the host:

```sh
scripts/run-qemu.sh --network-mirror
```

The runner discovers the active default route at launch. XINA must be configured
with the host's IPv4 address and mask, the real gateway, and the host's DNS
server. For example, a host using `192.168.1.26/24` through `192.168.1.1` uses
those same values in XINA. They are examples, not hard-coded defaults.

The duplicate address is internal to libslirp: the guest is not attached
directly to the physical LAN. Slirp translates its traffic through host sockets,
so this works over Wi-Fi without root, TAP devices, capabilities, firewall
rules, or a separately installed daemon. It was validated from XINA against
both the mirrored gateway and an Internet address with no packet loss.

A remote LAN client appears to XINA as a neighbour on the same subnet, while
libslirp's inbound `hostfwd` path does not normally proxy that
overlapping-subnet return path. Encore adds a narrow proxy-ARP filter in mirror
mode: it answers XINA's neighbour request with libslirp's learned gateway MAC.
The resulting IP packet is then handled by libslirp's existing NAT path. Encore
does not rewrite IP packets or checksums and does not switch to another network
transport when a service is exposed.

This mode deliberately does not rewrite XINA's configuration inside the
emulated Ethernet card. Doing that below the guest IP stack would require a
second ARP/IP translation layer and checksum rewriting. XINA therefore remains
the source of truth for its own network parameters.

## Unprivileged passt transport

The experimental `passt` path replaces libslirp with a separate, maintained
user-mode networking daemon:

```sh
scripts/run-qemu.sh --network-passt
```

The runner installs the distribution's `passt` package when required, creates a
private Unix socket, starts the daemon as the invoking user, and connects QEMU
through its `stream` netdev. Both processes remain unprivileged. The socket and
daemon disappear when QEMU exits.

By default, `passt` derives its advertised topology from the host. XINA must be
configured with the same IPv4 address, mask, gateway, and DNS values shown by
`passt` when the runner starts. The apparent address sharing is intentional:
`passt` translates the guest's Layer-2 traffic into host Layer-4 sockets rather
than placing a second machine with that address on the physical LAN.

```text
host           192.168.1.26/24
XINA           192.168.1.26/24
gateway        192.168.1.1
```

The values above are only an example. They must not be hard-coded; roaming to
another network changes the topology that `passt` presents.

Explicit service mappings use the same options as NAT:

```sh
scripts/run-qemu.sh --network-passt --forward 8080:80
```

Unlike a bridge, this still translates guest traffic through host sockets and
does not give XINA an independently owned LAN address. It can nevertheless
share the host's normal Ethernet or Wi-Fi connectivity without TAP devices,
raw sockets, capabilities, or firewall configuration.

## Existing Linux bridge

Advanced installations can place XINA directly on a real network:

```sh
scripts/run-qemu.sh --network-bridge br0
```

`br0` must already be a Linux bridge. During its normal root preparation phase,
the runner creates the persistent `encore-p2k0` TAP, assigns it to the selected
runtime user, and attaches it to that bridge. QEMU then opens the TAP as that
unprivileged user. This does not depend on a distribution QEMU package,
`qemu-bridge-helper`, setuid programs, or extra capabilities on the bundled
binary.

The TAP carries an Encore marker. The uninstaller removes it only when that
marker still matches; an unrelated interface is never adopted or deleted.
For an installed cabinet profile, a small root `oneshot` service replays this
same runner-owned preparation at boot, before the cabinet login path starts.
Encore deliberately does not create the bridge or change NetworkManager or
systemd-networkd configuration. Wi-Fi interfaces also commonly cannot provide
a transparent Ethernet bridge.

This mode has no QEMU port forwarding. Give the game a static address suitable
for that LAN and connect to it directly. `--http-port` is rejected because it
belongs to the isolated user-network mode.

> [!WARNING]
> Bridge mode exposes XINA's 1999 TCP/IP stack, HTTP server and optionally
> Telnet service directly to the attached network. Use a dedicated trusted
> cabinet VLAN or an equivalent firewall boundary, never an untrusted LAN.

## DHCP and automatic addressing

XINA does not contain a DHCP client in the validated game path. A DHCP server
beside the emulator therefore cannot make the guest adopt a lease: the guest
must first know its own IP address, mask and gateway.

Hard-coding the current resource addresses from the host would work for one
specific update, but those addresses differ between games and releases. It
would also bypass the game's persistence model. Encore therefore does not
inject network adjustments today.

A future compatibility helper has three possible designs:

1. **Version-aware adjustment writer.** Update the same persistent resources
   as the operator menu, with signatures and read-back validation for every
   supported game release. This offers automatic configuration but has the
   largest maintenance and corruption risk.
2. **Packet-level address translation.** Keep a documented fixed guest address
   and translate it to a host-selected address. This is robust and avoids game
   memory changes, but it is NAT rather than DHCP and cannot make XINA display
   a dynamically leased address.
3. **New guest DHCP support.** Add a DHCP client to the historical guest code.
   This is the cleanest guest-visible result but requires patching/rebuilding
   each game and is outside the emulator's hardware boundary.

The preferred direction is packet-level translation plus automatic host-side
port discovery. It modernizes connectivity while leaving XINA's saved
adjustments intact. A version-aware writer may remain an explicit convenience
option after its persistence format is understood and tested; blind RAM
injection should not become a product feature.

Rough implementation costs are:

| Approach | Prototype | Supported implementation |
|---|---:|---:|
| Signature-checked live adjustment override | 2–4 days | 1–2 weeks across supported updates |
| Fixed guest IP with host-side translation | 4–7 days | 2–3 weeks including ARP and bridge tests |
| DHCP-proxy illusion with leased external address | 1–2 weeks | 3–6 weeks across SLiRP, TAP and bridge paths |
| DHCP client added to the guest | several weeks | unsuitable without a maintained guest-code fork |

The DHCP-proxy design would lease an address on XINA's behalf and rewrite ARP
and IP traffic at the emulated-card boundary. It can make the cabinet reachable
through a dynamically assigned host-side address, but XINA would still retain
an internal static address. Calling that mechanism native guest DHCP would be
misleading.

---

← [Documentation index](README.md)
