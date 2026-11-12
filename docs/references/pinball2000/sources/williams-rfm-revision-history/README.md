# Williams Revenge From Mars revision history

> [!NOTE]
> Preserved from <https://www.planetarypinball.com/mm5/Williams/tech/pin2000/software/rfm_history.html> on `2026-11-11`. This Markdown is a readable derivative; `raw/` contains the captured evidence.

## Revenge From Mars - Revision History

Version 1.50 - July 31, 2000
*(Changes From 1.40)*

> Updated to latest operating system.
>  Enhanced coin/bill handling and credit system.

Version 1.40 - January 31, 2000
*(Changes From V1.30)*

> Fixed a bug in Martian Bowling that sometimes caused the game to reset.
>  The Bonus Wave total page now always runs, even if the Jet Exit Post is disabled.
>  Fixed a bug in Martian Happy Hour which caused some of the martian animations not to appear.

Version 1.30 - November 24, 1999
*(Changes From V1.20)*

> Added Martian Champion.
>  Added a jet bumper rule.
>  Added a ball saver (for the start of a new ball).
>  Added a victory lap rule. (Similar to Attack From Mars, started when
>  Attack Mars has been won.
>  Added the ability to loop the ball under certain conditions.
>  (Scenes where the shot does nothing, or if LOCK is lit let the ball loop around.)
>  Adjustments are added to control this behavior. (Default: OFF)
>  Added an adjustment to let the ball loop through the loop shots during bonus wave sudden death.
>  Adjustment "Bonus Wave Ending" has 2 settings:
>  "Ramps+Loops" (original behavior) and ""Ramp Only" (new behavior)
>  More "Family Mode" logic added.
>  Added service credit switch debounce.
>  Fixed a bug at the start of Mother Ship that sometimes caused no balls to kick out
>  at multiball start.

Version 1.20 - June 9, 1999
*(Changes From V1.10)*

> Restored United Kingdom to country list (missing in version 1.1)
>  Fixed Canadian dollar coin bonus pricing.
>  Added display of game/serial number and generally reorganized
>  the System Information screen.
>  Added timestamps for tilt and replay.
>  Added adjustments for awarding saucer lights for some
>  number of martians killed.
>  Added attract mode sounds and adjustment.
>  Attract mode and game over display effects now take note of the
>  'Insert Coins' adjustment and do not display that phrase if the
>  adjustment is set to 'no'. This is for places which use swipecards.
>  Fixed a bug in the custom message display which showed garbage if
>  a message line used the last available character on the line.
>  Modified the attract mode pricing display to show 2 lines per page,
>  with intelligent skipping of empty lines. If a given page has only
>  1 line to display, it is centered.

Version 1.10 - May 24, 1999
*(Changes From V1.00)*

> Timing of flipper circuits changed to make the flippers run cooler.
>  Hourly bookkeeping data extended to cover 7 days.
>  Changes made to pricing modes for Norway, Netherlands and Holland.
>  Adjustment printout now shows replay information and dip switch settings.
>  Replay boost is now available when game is set to give credits for score awards.
>  Game Name and version displayed in "coin door open" messages.
>  Lamp effects added for Multiball Start and Multiball awards.
>  Autopsy now tosses out items in a fan-out fashion, starting with the center shot.
>  Switch compensation improved in various places.

Version 1.00 - May 5, 1999
*(Changes from V0.90)*

> System changes made to eliminate lockups and resets. This version is extremely stable.
>  Hypno-Beam multiball awarded by Stroke of Luck
>  3 New modes for the "?" (5th round):
>
> > Martian Bowling
> >  Martian Autopsy
> >  Martian Tank
>
> Attack Mars mode has been completed.
>  Pressing flippers now cancels certain effects.
>  Many difficulty adjustments implemented.
>  After a round is completed, the introduction of subsequent rounds will not run if the ball is in play (not captured by post on playfield).
>  Sound and Graphics added to Match.
>  Sound added to Enter Initials.
>  Logic for Flipper and Action button broken switch compensation improved.
>  Attract mode messages and Pricing messages have been implemented and may be entered from test mode using test buttons or a keyboard.
>  Score Award 2 may now be set to "credit"
>  Automatic percentaging implemented for replay score management.
>  "Empty Balls" test added.
>  When the power driver board is not detected, a message is displayed indicating that this board is not connected or F108 has a problem.
>  Audit totals are now maintained for each type of credit that may be awarded (HSTD, Score, Match and Special).
>  "Switch Trace" command added to shell to allow a real time switch trace during operation.
>  Solenoid test now properly flips the Flipper coils
>  Keyboard control of test system (including help map and "flip" feature) implemented.
>  Fuse values now shown in test report.
>  Hourly earnings chart now shows hourly earnings for the last 7 days.
