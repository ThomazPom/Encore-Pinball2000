# Software updates and flash

Pinball 2000 replaced routine EPROM swaps with updateable flash on PRISM. The
platform supports more than one update path, but they converge on the game and
sound flash content rather than replacing the masked-ROM art banks.

## Factory update path

The documented serial workflow is:

1. connect a null-modem cable to the coin-door DB-9 while the game is off;
2. boot the game and connect with the Update Manager;
3. select a compatible game update and transfer it;
4. leave the connection untouched after the progress bar reaches its end;
5. wait for the machine to reset, return to attract mode and reconnect for
   verification.

The factory explicitly warns that the progress bar alone is not completion.
See the [Williams instructions](../sources/williams-update-instructions/README.md).

## What an update does and does not prove

The masked banks, game flash and sound flash are separate integrity domains.
An update package can carry a complete replacement for the updateable game
portion while still expecting the correct masked image banks. Therefore:

- “the update contains the whole current game program” does not imply that it
  replaces all masked assets;
- a masked-ROM checksum failure can coexist with a bootable flash update;
- successful transfer is distinct from post-flash compatibility checks;
- old factory images and later community images must not be treated as
  interchangeable merely because their version labels look alike.

The factory [RFM](../sources/williams-rfm-revision-history/README.md) and
[SWE1](../sources/williams-swe1-revision-history/README.md) histories establish
released change claims. The [community update log](../sources/mypinballs-rfm-code-updates/README.md)
is a separate evidence class.

> [!NOTE]
> Encore's supported CLI, serial exposure, captured-flash pinning and fresh-run
> semantics belong to the main Encore documentation. These preserved pages
> describe original-machine behavior and historical tools; they are not the CLI
> reference.

## Other historical paths

Williams also described an ISA POST/PUB update mechanism. First-hand accounts
say the inserted ISA board could take over during POST, program flash and show a
simple progress indication. That path explains why development and field-update
material sometimes refers to an “update board” even though serial update is the
normal owner workflow.

[Architecture](architecture-and-development.md) ·
[Hardware](hardware-and-service.md) ·
[All preserved sources](../sources/README.md)
