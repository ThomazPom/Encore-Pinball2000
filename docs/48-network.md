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

Restart the game after changing its network adjustments. During development,
the XINA console command `net start` can start the configured stack without a
full reboot.

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

## Existing Linux bridge

Advanced installations can place XINA directly on a real network:

```sh
scripts/run-qemu.sh --network-bridge br0
```

`br0` must already be a Linux bridge and QEMU's bridge helper must permit the
current user to attach to it. Encore deliberately does not create the bridge,
change NetworkManager/systemd-networkd configuration, or grant bridge-helper
permissions. Wi-Fi interfaces also commonly cannot provide a transparent
Ethernet bridge.

The runner locates the distribution helper in the conventional
`/usr/lib/qemu`, `/usr/libexec/qemu`, or `/usr/local/libexec/qemu` locations
and passes its exact path to QEMU. The host administrator remains responsible
for `/etc/qemu/bridge.conf` and for the helper's privilege policy. Encore does
not make a packaged helper setuid or grant it `CAP_NET_ADMIN` automatically.

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
