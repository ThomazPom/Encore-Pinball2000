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

---

← [Documentation index](README.md)
