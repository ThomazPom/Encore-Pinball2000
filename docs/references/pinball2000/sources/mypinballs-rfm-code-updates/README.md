# myPinballs Revenge From Mars code update log

> [!NOTE]
> Preserved from <https://www.mypinballs.com/software/rfm/code_updates.jsp> on `2026-11-11`. This Markdown is a readable derivative; `raw/` contains the captured evidence.

This is the update log for our Revenge From Mars Software update. This update is an unofficial fan made update and is given for free to the community. Install at your own risk. No warranty is given or implied. We hope you like the changes.

If you would like to support future software updates and mypinballs electronics in general, please consider donating :|) Thanks

![pixel.gif](assets/pixel.gif)

### Update v2.60

**Game Changes**
 **This update and updates higher will require the full 4 opto pcb hardware expansion to operate correctly**

 System - Updates to support new physical lock hardware
 Multiball - Updates to support physically locking 3 balls
 Multi-devices - Updates to the Right Lockup class to support physically locking 3 balls
 Mothership - Shaker effects added
 Mothership - expanded the list of ships to destroy
 Mothership - adult setting for some more risqué motherships to blow up :)
 Autopsy - Shaker Effects added
 Tank - Shaker Effects added
 Bowling - Shaker Effects added
 Bowling - More background log outputs added to assist with general ‘bowling bug’ identification
 Invaders - Shaker Effects added
 Mystery Mode - Adjusted the percentages of each mode likeliness to be 25% each
 Attract - Added Tournament Score screen to the main attract display effects
 Tournament - Updated display effects to run once only and updated framework to allow global use
 Settings - Shaker Power default changed to 100

### Update v2.50

**Game Changes**
 Double Scoring - NEW FEATURE! Awarded from the stoke of luck
 Capture Multiball - Now can use smart bombs to add 3 additional balls into play if trough is upgraded
 Alien Abduction - Improved the family setting logic for various speech calls. The more dubious calls outs are removed now with the setting on. Before some callouts were still in place regardless.
 Bottom Lanes - Improved the family setting logic for various speech calls on drain. The more dubious calls outs are removed now with the setting on. Before some callouts were still in place regardless.
 Hurry Up- Improved the family setting logic for various speech calls. The more dubious calls outs are removed now with the setting on. Before some callouts were still in place regardless.
 Shaker - Added new adjustment for power percentage to allow fine tuning of intensity for effects

### Update v2.24 (v2.42)

**Game Changes**
 Ball Save - New transparent mode start font overlay graphic screen instead of std text
 Martian Attack - New London scene background option
 Multiball - New abandoned amusement park scene background option
 Extra Ball - Use the animation for replay awards instead of text
 Bonus Wave - Added Shaker effects for bonus award shots

**XINA 1.36**
 System - Updates to allow future game updates to be max 8mb in size instead of max 4mb

### Update v2.23 (v2.40)

**Game Changes**
 Party Mode - Added new Party mode - Score War. When this mode is enabled all shots to the stroke of luck hole when lit award 10% score reduction for other players. Games must have 2 or more players active for it to be enabled as well as having the setting enabled. Only play with people you are friends with and get on well with!! :)
 Party Mode - Updated text on settings that require a reboot to be enabled
 Stroke Of Luck - Added new award - Odd Change awards a random score award from 2500 & 10Mil
 Stroke Of Luck - Added new award - Add 10% Player Score
 Stroke of Luck - Improved Collect Bonus Integration. End of Ball Graphics now play when awarded aswell as receiving the score calculated
 Stroke of Luck - Added new award : Reduce other players scores by 10%
 Stroke of Luck - Added new Award : 2 Martian Bombs
 Stroke of Luck - Added new Award : +10X Bonus
 Happy Hour - When drunk flippers restore from a multi ball mode ending the ball save timer is started to catch any mishaps with the change back.
 Power Drain - fixed a minor issue where the feature may still run at the end of multi-ball if it was queued up previously. Ie it wasn't been canceled cleanly
 Right Popper (Stroke of Luck) - Improved logic to fix a bug where a payback time award would conflict with a lit martian attack mode and result in no ball being kicked out for the payback time duration
 Mothership - is now trough aware and will launch 1 - the max number of trough balls at mode start. i.e 3 or 5.
 Payback Time - Fixed a bug where the mode could be started more than once
 Payback Time - Fixed a bug where the alternating left and right shots were not alternating correctly
 Payback Time - Fixed a bug with the playfield lamps showing what shot was lit
 Alien Abduction - Added more speech, some adult!
 Attract - Added more speech to flipper button presses
 Paris in Peril - Added more Speech
 End of Ball - Added more Speech
 Ball Save - Added more Speech
 Secret Weapon - Added more Speech

**XINA 1.35**
 PinGame - Added new method score_player_reduce() to allow reduction of player scores for new features and inter player battles

### Update v2.22 (v2.30)

**Game Changes**
 Power Drain - New Feature! Inspired from Dialed In. During Martian Attack Multiball certain shots must be completed in time otherwise the big martian will attack your flippers for a short period!!
 Midnight Madness - Updates to support multiple sizes of troughs
 Capture Multiball - add a ball now supports multiple sizes of troughs
 Scene - Fixed an issue where the ball save was broken for scene endings. Where the ball drains and is not held by the up post as the scene end runs.
 Hurry Up - Improved graphics interaction with scene manager, especially at end of scenes where a hurry up is still running.
 Hurry up - Now gets stopped when the scene ends if its not been collected and is still running
 Happy Hr - regular flippers are now restored if a multi-ball mode runs during this mode, pausing the happy hr mode
 Happy Hr - Drunk flippers are resumed once any higher priority mode has finished (multi ball) modes. Possibly makes the games alittle more tricky, so players need to be alert!
 Happy Hr - Added new graphics and speech calls to tell players about normal flippers if changing from drunk and vise versa
 Attract - Fixed a few missing lamps from the std twinkle effect
 System - 6 ball trough now recognised once extra hardware installed
 System - Reverted to original v1.5 Sound File

**XINA 1.34 Update**
 System - Added new additional baud speeds new updater software program
 Hidden Images - Removed some old images to make more flash ram space for game code updates and allow original computer systems to still receive the latest code updates

### Update v2.21

**Game Changes**
 Attract - Added new high score screen for saucer lights collected
 Attract - Added new high score screen for saucers destroyed during multi ball
 Saucer Lights - added monitoring and storage of saucer lights per player game for hs use
 Multiball - added monitoring and storage of saucers destroyed per player game for hs use
 Multiball - fixed a bug where some ship destroyed shots would not shake the shaker

**XINA 1.33 Update**
 Credits - Added a new setting to allow reset of the credits at boot
 Credits - Credits are now cleared on boot if the setting is set
 System - Added some new colour definitions - Purple, Turquoise, Brown

### Update v2.20

**Game Changes**
 Attract - Added mypinballs logo to software version screen
 Attract - Added clock and date display with adjustments
 Attract - Added midnight madness ready detection to clock display
 Attack Mars - Lowered default champ score to 200mil
 Scene - Added midnight madness ready detection
 Martian Bombs - Added new playfield lamp effect to aid player when martian bombs are lit (using shooter lane GI lamps)
 Midnight Madness - Added New Mode. Start a game close to midnight'
 Tower Struggle - Added more shaker effects - To martian splats and saucer explodes
 System - Fixed problem with optional martian lamps showing up in lamp test as broken
 Bonus Wave - added new lamp effects for active wave using saucer and mode feature lamps
 Bonus Wave - added new lamp effects to show global game progression during mode
 Drive in Demolition - added More shaker effects to tank and car/truck weapons
 Party Modes - New feature. Select special flipper options for party play
 Happy Hour - Added new feature for 'drunk' flippers

**XINA 1.32 Update**
 Lamps - Add option to ignore lamp test. For use if LEDs are installed
 Lamps - Added experimental RC charge and decay values for better LED lamp testing
 System - Improved some Adjustment setting housekeeping
 System - Added new menu item for party modes
 Flippers - amended the logic for bad eos switch flipper retriggering as this conflicts now with a no hold party mode.

### Update v2.10

**Game Changes**
 Quick-Shot - Feature lamp effects improved for active hurry up
 Quick-Shot - Added timer pausing for when ball enters/exits jets and upper lanes
 Quick-Shot - Added timer pausing for when ball enters exits stroke of luck
 Quick-Shot - fixed bug where some scenes background graphics were lost after hurry up ended (achieved or not)
 Quick-Shot - Added speech for start and shot success
 Quick-Shot- Added shaker effects for shot success
 Question Mark - Percentage of mode selection updated. Autopsy now most likely mode
 Question Mark - Martian Aerobics, renamed Martian Invaders
 Capture Multiball - Added sneaky lock acknowledgement (lock from rear when lit avoiding diverter)
 Capture Multiball - Added speech for ball locks
 Capture Multiball - Added option for add_a_ball
 Capture Multiball - Added add a ball display effect/ graphics
 Bottom Lanes - added add a ball logic in conjunction with stroke of luck during multi ball play
 Stroke of Luck - added add a ball logic during multi ball play (main, martian, bonus etc)
 Circle Shots - Missiles are now only collected/fired when there is an unlit missile circle insert on the playfield
 Circle Shots - Completing a set (4) missile shots now ups the bonus multiplier by 1 each time (on that ball)
 Attract - updated lamp patterns to fix missing saucer rim 9 lamp on main pattern strobe
 Attract - updated lamp patterns on searchlight pattern to fix some missing lamps and some in wrong section
 Skill Shot - Fixed Super Skill Shot activation which was broken
 Outlane - Improved sound and speech calls logic
 Bonus Wave - Changed balls in play start to 2
 Bonus Wave - Added option for add_a_ball
 Mothership - Changed balls in play start to 3
 System - Added shaker hardware control
 System - Added knocker hardware control
 System - Added adjustments for shaker installed
 System - Added Adjustments for knocker installed
 Quick-Shot - Added extra sound for mode start
 Hypno Beam - Fixed a bug where the new champion score would not be recorded
 Hypno Beam - Reworked (lowered) the shot levels now there is a champion score to make destroying multiple beams more interesting and worthwhile
 Hypno Beam - Adjusted extra ball award for destroyed beams to 4
 Bonus Wave - Disabled add a ball when entering sudden death (1 ball in play)
 Capture Multiball -Added shaker effects for super jackpot and regular jackpots
 Martian Multiball - Added shaker effects for hitting and destroying large martian
 Drive in Demolition - Added shaker effects for nuke
 Drive in Demolition - Removed the mode start points (2 mil) seeing as its now startable from game start
 Tower Struggle - Added shaker effects for tower collapse
 Secret Weapon - Added shaker effects to end scene, upper cut and mothership explode
 Alien Abduction - Added shaker effects to ramp shots and destroying centre saucer
 Martian Happy Hour - Added shaker effects to martian kills
 Big O Beam - Added shaker effects to barn hits
 Paris in Peril - Added shaker effects to martian kills and catapult hits
 Mars Needs Women - Added shaker effects to console destroy and glass crack
 Payback Time - Roughed in first pass of this mode - enabled at sol after main multi ball and martian multi ball completed
 Secret Weapon - Fixed a bug with the update of martian rejuvenation on the energy bar graphics

**XINA 1.31 Update**
 Reverted to 3 characters for high scores. Some game specifics are hard coded for 3
 Added real knocker option instead of simulated sounds when enabled

### Update v2.0

**Game Changes**
 Quick-Shot - Hurry up feature created - Shoot drop target to collect countdown score
 Quick-Shot - Graphics animations (Mothership used from martian aerobics), text and boxes created.
 Quick-Shot - Progressive scoring, Each successful award increases start value
 Secret Weapon - altered the amount of speech in the mode when shots made
 Secret Weapon - added ROUND X FIGHT speech with adj option to mode start
 Secret Weapon - added stooge fight move to ramp shots
 Secret Weapon - decreased punch and kick move effectiveness for easy centre shots
 Secret Weapon - Added language translations for name and settings
 Secret Weapon - Saucer light now requires completing all robot-lincoln moves
 Secret Weapon - Martian now rejuvenates if Abe is idle for too long
 Circle Shots - Missile Champ Added
 Circle Shots - Missile awards now offer Martian bombs at certain thresholds
 Circle Shots - Missile awards now offer Ball Save restart at certain thresholds
 Circle Shots - Missile awards now offer Quick-Shot at certain thresholds
 Circle Shots - Missile points awards are bigger and easier to get, plus there more of them
 Circle Shots - Missiles total now awards a champ entry if beaten
 Hypnobeam - Beams destroyed now awards a champ entry if beaten
 Attract - Added new lamp show effect - searchlight
 Attract - Added new lamp show effect - sweep up and down
 Attract - Updated the regular playfield lamp strobing effect
 Attract - Missile Champ Screen Added
 Attract - Hypnobeam Champ Added
 Attract - added more speech call options to flipper button presses
 Attract - Removed Dillon Tribute to gain more ram space
 Scene - Now can start from the saucer modes in the centre
 Ball Save - Balls are now autolaunched from shooter lane at correct time
 Service Mode - updated video test alignment image to save ram, 200k freed
 Service Mode - cleaned up brightness/contrast levels on video test alignment image
 Stroke of Luck - Added ball save award
 Stroke of Luck - Added collect bonus award
 Stroke of Luck - Added 2 million award
 Stroke of Luck - Removed saucer light award

**XINA 1.30 Update**
 Video test alignment graphics optimised and cleaned up to free valuable flash memory
 Test system info menu updated with more colours for versions
 Test system info now states 'custom build' - lets not upset anyone! ;)
 Ball Saver class changes for better game autolaunch in RFM build

 *Will run on pinbox and original hardware
