# 49 — Tournament server preservation

Pinball 2000's optional network stack supported more than its built-in HTTP
status page. The Expo 1999 system associated barcode badges and player images,
accepted scores automatically and sent standings back to cabinets for attract
mode display.

## What survives

- Contemporary accounts identify the original tournament server as a Java
  program written by Lyman Sheats. A 2000 post from Tom Uban encouraged release
  of that source, but no verified public source archive has been located.
- A 2009 preservation page stated that complete tournament software and setup
  instructions had become downloadable. Its two Pinballz forum links are now
  dead, and no recoverable package has yet been identified.
- The independent myPinballs Tournament System later implemented a compatible
  worldwide RFM service. Its v1.2.2 website still exists and reported renewed
  investigation in December 2023 after thirteen years of inactivity. Public
  pages and setup material survive; its server source has not been found.
- A separate Pinball2000scores service was reported online in 2014 but its
  public domains no longer answer.
- A Nucore tournament server was demonstrated in 2010 and announced for a
  future open-source release. No corresponding public source repository has
  yet been verified.

Primary surviving references:

- <https://www.pinball2000.de/pin2000_misc.htm>
- <https://www.mypinballs.com/tournament/>
- <https://www.mypinballs.com/tournament/core/faq.jsp>
- <https://www.mypinballs.com/tournament/core/techsetup.jsp>
- <https://www.mypinballs.com/tournament/core/gamesetup.jsp>
- <https://www.pinballnews.com/shows/expo2010/index4.html>

## Recoverable wire protocol

The game is a TCP client. It connects to the configured tournament address on
port 2069 and exchanges binary request/response messages. Current symbols and
code analysis expose an 8-byte header and these operations:

| Type | Observed purpose |
|---:|---|
| 0 | standings/display payload |
| 1 | game-over score submission |
| 2 | barcode/card scan |
| 3 | main tournament information |
| 4 | division information |
| 5 | players in a division |
| 6 | player's division |
| 7 | player image |
| 8 | player information |
| 9 | cabinet power-up announcement |

The client code reveals message lengths, byte order, acknowledgement types and
most response structures. A packet capture against a surviving compatible
server would still be valuable, but a clean reimplementation does not depend
on recovering the original Java source.

## Redevelopment estimate

| Scope | Estimated focused work |
|---|---:|
| Protocol probe accepting power-up and one score | 1–3 days |
| Local usable server with players and persistent rankings | 4–8 days |
| Historical experience with badges, divisions, images and attract standings | 2–4 weeks |
| Public hardened service with authentication, operations and backups | 4–8 weeks |

The first preservation target should be local and isolated:

```text
game → TCP 10.0.2.2:2069 → local Encore tournament service
```

A positive milestone is a real game-over packet being stored, acknowledged and
returned as a ranking that the unmodified game displays. Internet hosting,
accounts and federation should follow only after that protocol fixture is
reproducible.

---

← [Optional network card](48-network.md) · [Documentation index](README.md)
