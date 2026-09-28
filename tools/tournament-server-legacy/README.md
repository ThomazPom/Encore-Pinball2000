# Legacy Pinbox tournament server

This directory preserves the Python 2 community server used to validate
Encore's JTS transport. It is a research tool, not a production service and
is not started by the Encore launcher.

> [!WARNING]
> The recovered source has no explicit licence. Its original header names
> `MasterGeek` as author and dates it 2013. Keep it out of release archives
> and redistribution until its licence or authorisation is established; see
> [NOTICE.md](NOTICE.md).

## Run a controlled test

Requirements:

- Python 2.7;
- a deliberately controlled HTTP API implementing `get_scores` and
  `post_score`, or acceptance that the score list will remain empty;
- UDP/2069 reachable from the emulator host.

From this directory:

```bash
P2K_TOURNAMENT_API=http://127.0.0.1:18081/ \
  python2.7 ./server.py
```

`P2K_TOURNAMENT_PORT` overrides the default UDP port 2069. The server binds
all local addresses and uses clear-text HTTP, so run it only on a trusted lab
network. Stop it with `Ctrl-C`.

The optional `pb.i64`, `rfm.i64` and `swe1.i64` player-picture fixtures from
the recovered package are intentionally not vendored: they have no established
licence. If a controlled API returns players and the guest requests pictures,
place authorised copies beside `server.py`. The observed fixture hashes were:

```text
11ac62760f82690adcc48d99aa3426b0bb7bf2c383e237c465f1efa14ae63eff  pb.i64
16f39f745612d41ab41499edb82618b5bb93d9f3606f776b88a651dba384b40c  rfm.i64
54dc4f967982365539ec7c4db9440aebb18c73a73a5bbfb51050987386328b59  swe1.i64
```

## Encore modifications

The recovered relay generated an unrelated transaction counter in replies.
The preserved guest validates the 16-bit transaction ID and discarded those
otherwise valid datagrams. `assemble_reply()` now copies request bytes 2–3
into every structured reply.

Encore also changes the default listener from the recovered value 2070 to the
guest's actual destination port 2069, and exposes the listener port and HTTP
API base through environment variables. The protocol body remains the legacy
Python 2 implementation.

Under Slirp, Encore separately handles the guest's asymmetric receive
convention: JTS transmits from an ephemeral UDP port but listens for the reply
on UDP/2069. That emulator correction is described in the
[tournament-server research](../../docs/49-tournament-server.md).

## Validation record

On 2026-09-27, RFM 1.90 completed power-on and types 3, 4, 5, 8 and 7 against
this transaction-ID fix and a local fixture API. A packet capture showed guest
requests at TTL 64 and valid reply checksums. This proves the exercised lab
path; it does not prove production safety, historical server equivalence or
all request types.
