# Networking and tournament operation

Networking was part of the original XINU/XINA design, not a later emulator
invention. Development used Ethernet/TFTP, and the 1999 Expo demonstrated a
multi-machine tournament with player identification and centralized scores.

## Original game-side pieces

| Piece | Preserved evidence |
|---|---|
| Ethernet | SMC EtherEZ ISA cards, with the SMC8416T family repeatedly documented |
| services | XINA shell/telnet, a small HTTP server and game-side network settings |
| tournament client | game sends player/score traffic and can receive ranking data |
| identity | optional RS-232 barcode reader on COM2; free-play operation can omit it |
| settings | tournament enable, local addressing, tourney server address and communication assignments |

The [GerWiki XINA page](../sources/gerwiki-xina/README.md) is the broad command
and networking reference. The [game setup](../sources/mypinballs-tournament-game-setup/README.md)
and [technical setup](../sources/mypinballs-tournament-technical-setup/README.md)
record a later Internet deployment.

## What the 2008 system demonstrated

The contemporary [Pinball News report](../sources/pinball-news-rfm-online/README.md)
records that unmodified RFM 1.5-or-later game code could participate, that a
barcode was optional, and that the operational exchange was deliberately
small—player identity and score sent outward, rankings returned to the game.
The router workarounds were compensating for an original flat-LAN assumption,
not adding the tournament client to the ROM.

## Historical setup versus Encore

The old instructions used a specially configured router, fixed server address
and inbound ports 2069, 23 and 8080. Those values are evidence for that
deployment, not universal protocol requirements. Encore's default rootless NAT
changes the topology: a guest packet must cross a router hop that did not exist
on the original LAN.

> [!CAUTION]
> Do not copy old public IP addresses, telnet credentials or firewall rules into
> a current installation. Use Encore's current network documentation and expose
> only the services actually required.

## Confidence boundaries

- Hardware models and menu paths are corroborated across multiple sources.
- Exact server protocol semantics are strongest where verified by Encore packet
  captures or surviving server code; prose alone is not a wire specification.
- The assertion that a particular ROM contains a given network feature remains
  version-specific.

[Architecture](architecture-and-development.md) ·
[Software updates](software-updates.md) ·
[All preserved sources](../sources/README.md)
