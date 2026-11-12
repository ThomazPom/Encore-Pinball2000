# Pinball2000.de miscellaneous technical notes

> [!NOTE]
> Preserved from <https://www.pinball2000.de/pin2000_misc.htm> on `2026-11-11`. This Markdown is a readable derivative; `raw/` contains the captured evidence.

Misc. Information about PIN2000:

Version 3.09
Updated: Ethernet #2 with SMC setup info, Easteregg

Hint: Scroll down to find all pin2000 infos or use the tags.

[1) RFM / Ep1 Swap](https://www.pinball2000.de/pin2000_misc.htm#Swap)
[2) The Pin2000 Easteregg](https://www.pinball2000.de/pin2000_misc.htm#Easteregg)
[3) Next Pin2000 Titles](https://www.pinball2000.de/pin2000_misc.htm#Next_Pin2000)
[4) Work on Ethernet I](https://www.pinball2000.de/pin2000_misc.htm#Ethernet_1)
[5) The Playfield Glass](https://www.pinball2000.de/pin2000_misc.htm#Glass)
[6) Adding Headphones](https://www.pinball2000.de/pin2000_misc.htm#Headphone)
[7) Hexkeys for BB Mounting](https://www.pinball2000.de/pin2000_misc.htm#Hex_Keys)
[8) Bolts for BB Mounting](https://www.pinball2000.de/pin2000_misc.htm#Bolts)
[9) Is there a Knocker?](https://www.pinball2000.de/pin2000_misc.htm#Knocker)
[10) When the Backbox does not open](https://www.pinball2000.de/pin2000_misc.htm#BB_not_opens)
[11) Strange parts...](https://www.pinball2000.de/pin2000_misc.htm#Strange_Parts)
[12) Work on Ethernet II](https://www.pinball2000.de/pin2000_misc.htm#Ethernet_2)
[13) Neonlight that nerves](https://www.pinball2000.de/pin2000_misc.htm#Neonlight)
[14) Server Tournament](https://www.pinball2000.de/pin2000_misc.htm#Tournament)
[15) Screen Problems during gameplay](https://www.pinball2000.de/pin2000_misc.htm#Screen_problems)
[16) SW Update Problems](https://www.pinball2000.de/pin2000_misc.htm#SW_Update_Problems)
[17) The Lockdown Bar](https://www.pinball2000.de/pin2000_misc.htm#Lockdown_Bar)
[18) Work on Ethernet III](https://www.pinball2000.de/pin2000_misc.htm#Ethernet_3)
[19) The Pinball2000 Serial #](https://www.pinball2000.de/pin2000_misc.htm#The_Serial_No)
[20) Country Specific Pinball2000 Aspects](https://www.pinball2000.de/pin2000_misc.htm#Country_Specific)
[21) Differences between Starwars and Revenge](https://www.pinball2000.de/pin2000_misc.htm#RFM / EP differences)
[22) The people behind Pinball 2000](https://www.pinball2000.de/pin2000_misc.htm#P2K People)
[23) Batman and Robin the 'custom' Pinball2000](https://www.pinball2000.de/pin2000_misc.htm#BatmanandRobin)
[24) Pinball2000 Euro pricing adjustments](https://www.pinball2000.de/pin2000_misc.htm#The_Euro_Pricing)
[25) Some Great P2K Links new 2009](https://www.pinball2000.de/pin2000_misc.htm#Some_Great_Links)

**1) General Upgrade Statement RFM <-> Ep1:**

See [RFM / Ep1 Swap](https://www.pinball2000.de/pin2000_rfm_ep1_swap.html)

**2) Pictures of Players on Screen:**

On Sat, 25 Mar 2000 08:45:21 GMT, daniellos@gmx.net wrote:

>i checked out the below combination....is it possible to change the
>picture or upload a pic? i had this idea: after somebody scored a
>highscore the operator could take a picture of the girl/guy with a
>digital camera and link the pic to the pin. would be cool to see the
>faces of the highscore-people in the pinball trailer...or even install

This was done, after a fashion, at the last Pinball Expo tournament,
with the SW:EI games. To enter the tournament, you had your picture
taken and printed on a badge with a bar code. To play a game, you
swiped your badge in a reader on the coin door of the game. When you
were done, it transmitted your score info to the master computer,
which automatically kept track of who was in what place in what
division. These standings were posted (complete with pictures of the
players) on a centralized screen, but also back on each of the SW:EI's
if it happened to sit in attract mode for very long between players.

Just a tiny peek at the directions we were heading. The day after the
tournament was over, so were these directions!

Duncan (Duncan Brown)
The code for the Easteregg is B R L RR L RRR L L
If you have SWE1 1.5 or RFM 1.6 you might want to try the code 'HMW'
to see another EasterEgg (HMW = B,8*R,L,13*R,L,23*R,LL)

**3) Next Pin2000 Titles:**

Hi,
Because I wanted to know the titles of game 3 & 4 of Pinball 2000 I
searched in the Trademark data base , which can be found at
http://trademarks.uspto.gov/ . I found these two new names Williams
Electronics Games, Inc had registered for pinball games :
nr name date
75-618550 WILD TOYS 99/01/05
75-663180 WIZARD BLOCKS 99/06/24

I wonder if these are the correct titles of game 3 & 4. Sadly we
won't see them.

Bye
Marcel

Update 18.9.2000

Info from Gene Cunningham (bought WMS Pinball) via Kyle Wren an RGP.

Title #3: Wizard Blocks, made until one playable whitewood playfield
Title #4: Playboy

**4) Work on Ethernet:**

This weekend, I got my RFM up and running on our LAN using the
information at http://members.home.net/ratherplaypinball/rfmnotes.htm
(great site!). We're now attempting to write a frontend for the shell
commands, with the ultimate goal being a frontend that can start a game,
launch balls and use the flippers. This would be combined with a good
webcam and MS Media Server stream to facilitate someone playing my RFM
over the Internet.

The practicality of this, of course, would be limited.. broadband
would definitly be necessary (I have 408kbit upstream capability at
home) .. also a good cam and fast streaming server. Also probably have
to lower the playfield angle a bit.. we'll see. It's more a simple
project just to see if it can be done, and how well it works.

Anyway, the sticking points are game starting and ball launching. It
seems like all the pinevents commands are ignored, as well as the
"start" command. Therefore, while we can use the "flip" shell command
to toggle flipper status, we cannot start a game or launch a ball via
the shell. Are pinevents commands just toggles to enable/disable the
referenced item, or should that command trigger the argument
(game_start, ball_serve, etc)? If current/ex WMS employees know, and
NDA's would prevent a direct answer to these questions, would it be
possible to at least confirm/deny that game start and ball launching are
possible thru shell commands in RFM 1.4?

Regardless of the outcome of this project, the ability to telnet into
the game while it's being played is great, if only for the world of
opportunities one has to mess with their (unsuspecting) friends while
they're playing <Very evil grin>. The game tilt, scenemgr and flip
commands alone can wreak havok on an unsuspecting player :) "Hey! These
flippers are possessed!! Tilt?!?! I never touched it! And why is it
starting Attack Mars already??" Muhahahaa :)

By Larry Scott

A screenshot from that action can be found under [larrys RFM online shot.](http://www.frpg.org/p2k.gif)

**5) Playfield Glass:**

I enquired with Steve Young about ordering a spare playfield
topglass for my RFM, figuring that if it ever breaks it will NOT be
easily duplicated, what with the reflective coating and all.

He replied that packaging and shipping it would be a monster
pain, and recommended I shop around elsewhere. I don't doubt it, or
his reluctance to mess with 'em.

Anyone know a disti or other source for these monster pieces
of glass, packaged for shipping? Closest big city to me is Seattle,
about 80 miles, so could drive to pick up there if available.

If you happen to know approx. price that would be handy as well.

Thanks,
 Jim
-
You should be able to get it from a Williams/Bally distributor in
Seattle. The cost is $200.

**6) Headphones**:

Rob Willemsen tried to connect wireless headphones (Sennheiser) in parallel to the speakers (directly at the amplifier board). He was successfull in doimg so and it is great! To protect the headphone from overvoltage damage when a speaker breaks, it seems to be useful to mount a 50Ohm Resistor in parallel to each speaker.

Subject:
 Re: Tech: Pin2K (SW:E1 to RFM conversion question)
 Date:
 Thu, 27 Apr 2000 17:02:54 GMT
 From:
 uban@my-deja.com
 Organization:
 Deja.com - Before you buy.
 Newsgroups:
 rec.games.pinball
 References:
 1 , 2 , 3 , 4 , 5 , 6 , 7 , 8 , 9

**7) Hex Keys for Backbox Mounting**:

From RGP 9.5.2000

Can anyone tell us the exact sizes of hex-keys that are needed to fix the
backbox onto a Pinball 2000? Are there two different sizes?
Someone wants us to get him a pair, but he doesn't know the exact size and
we can't take them into a shop to match them up.

There are three sizes, 7/32 outside cabinet to attach backhead and
1/4 inside to hold metal insert the 7/32 screws into. 5/32 holds the plastic
rest knob. Lloyd :)
(Lloyd R Olson).

Martin Wiest: On both Ep1 machines I recently found the hex keys were included in bypack

**8) Bolts/Bushings that hold backbox:**

From RGP 10.5.2000

Brian Brennan:
I got a Revenge From Mars new in the box recently, and while the
assembly was a breeze (unbelievably well designed machine), I noticed
that the 4 bushing/bolts aren't exactly "snug", even when tightened
completely. I can still jostle all 4 slightly with just my hand. Is
this normal? If so, any idea why?

Yes, this is normal. If you have a tightened assembly, a
malintentioned miscreant could loosen them up or even remove them from
the outside with a wrench. With the bolts being smooth and loose (and
at least from the factory, with a bit of loctite goop on the threads)
the *only* way those suckers are coming out is if someone has the
glass off and has the two proper hex wrenches.
Weird-looking, yes, but as designed.
Duncan Brown

**9) No Real Knocker:**

From RGP 14.5.2000 Answer from Louis Koziarz (PIN2000 Design team)

>Not to toot my own horn, but I'm very familiar with the sound of a
>standard knocker, and this ain't it.
Toot away. The knocker on RFM and SWE1 were done in software on the
DCS board. [same valid done for all WPC95 starting with CV]
>It's actually kind of
>unsettling. Call me crazy, but if this is the "normal" pinball 2000
>knocker, then I'll be looking for an old-style knocker very soon.
Well, you can rest easy, since there will be no more Pinball 2000
games.
For what it's worth, we actually had a real knocker on game 3. There
was an extra coil so we threw it in....
Louis

**10) Backbox does not open:**

From RGP 16.5.200
ELP!

Turned my SWE1 this weekend and heard that dreaded power supply fan
whine.
Long story short - NONE of my keys will open the backbox - either the
front OR back. I have both sets of keys, the coin door and the
playfield security lock, and they work fine, but neither of them fit in
the backbox locks.

Answer 1:
I had this problem with my RFM - the key that unlocks the back of the
backbox will indeed open the backglass lock.. problem is the locking
arm gets jammed over the CPU box and the lock won't turn. Jonathan
suggests sticking your arm back there (thru the back) and freeing it
manually.. since my game was already in place against the wall, I gave
the lock an extra twist with a pair of pliers before resorting to moving
it and the lock turned finally .. of course, this approach risks a key
broken off in the lock, so watch it!

Answer 2: Where are the Backbox keys:
Originally- the backbox key (equal for back and front) is attached to the wood
crossbeam that is bolted to the lower cabinet front side of the backbox. They are
attached with a nylon tie. If you were not the one to unpack the game- someone
else might have thought these keys redundant and tossed them :(
There are four screws holding the back cover on for shipping. Remove these
first- it might be unlocked (but doubt it)
marc b

**11) Strange Parts came with NIB P2000:**

From R.G.P. 22.5.2000

Question:
I just got one from Brady Distributing and when I was putting it together I
noticed a baggy with an extention cord about 10 inches long or so and a
little black connector with 4 wires coming out that go into a wirenut. I
can't find where it goes from looking in the manual. Anyone know?

Answer: (Jonathan Deitch)
the power cord is your convenience outlet adaptor - it plugs into the IEC
socket and provides you with a standard US outlet you can plug things like
soldering irons into.

the little black plug is a bill acceptor adaptor for certain model bill
acceptors so they run without harness enabling (otherwise the acceptor
would stay in a disabled state and never take bills)

**12) More Ethernet things:**
(by Martin Wiest)

I also installed an SMC 8416 Ethernet Card to my RFM. After disabling the
onboard sound etc. the ethernet card works fine. Afterwards I took a PC
with WIN98 and plugged in an ethernet card, installed Internet-Explorer
and started setup. A static IP adress has to be setup in the PC (normaly
ISPs need dynamic IP adresses). I selected 168.0.0.2 for the PC.
In the communication options of RFM the own IP adress has to be
set and I selected 168.0.0.1. After that RFM has to be rebooted!
After that TELNET could connect to RFM and also the IEX could
display the PIN2000 Webpage on RFM.
If ethernet still does not work, you might have to set the SMC Card
from Plug & Pray to a fixed setting using IRQ 10. This can be done
by using the DOS setup software of SMC and installing it together
with the SMC card into a DOS PC (more infos from Hans Balk www.shotmagain.nl)
The communication options of RFM offer lots of more settings.
**I detected when you enter the PCs IP adress as 'Tourney Adress'**
**and activate tournament mode, the game hangs up at game over**
**(and seem to try a webserver update). An IP observer tool finds 'attacks'**
**on my PC Port 2069 when RFM boots or ends game. The Port 5001**
**of Pin2000 wants to establish a UDP connection to Port 2069 of my PC.**
**The tournament functions already seem to be implemented on RFM 1.4!**
I also find in the game info display, that there is a card_no display for the current player,
also some info about tournament function! It would be great to get
any info about that webserver functionality needed for Pin2000 tournament!
See Pages of [RFM webserver](https://www.pinball2000.de/rfm_web.htm) and [Ep1 webserver](https://www.pinball2000.de/ep1_web.htm) .

**13) Light from Neon passes the monitor:**
from RGP 18.7.2000

Hey all-
Can you P2K owners help me figure out a part # or where to replace a
certain part? I've looked in the manual, don't see it..
If you look at your monitor, you'll see a black foamy pad which encircles
the monitor blocking the light from the head.. Since there's space between
the monitor mounting, this foam is used to make sure none of the
flourescant bulb light shows up on your game..
I wouldn't know this except that the piece along the top is missing, and
whenever the game is on, I see a long white strip of light on my playfield
because the foam is not blocking out the light there...
Just open the game and look for the black foam around the monitor if you
still don't know what part I'm talking about...
I need a replacement for it or a light blocking substitue..
Thanks for all recommendations & help!
Steve

Answer from Duncan Brown (one of the Pin2000 makers):

What happens is that the strip got stretched around the monitor when
originally installed...and eventually the glue lets go and the ends
shrink back away from each other to release the tension. Putting it on
with less tension, or even (for goodness' sake!) putting it on with the
gap on the other side of the monitor so when it comes apart you don't
have light in the worst possible place would have been great solutions.
Alas, with "modern" manufacturing priinciples, nothing is ever that
easy. That problem never got fixed before the games stopped being
produced. (There was a much simpler, non-sticky-foam based solution in
the works in engineering, but either of the above stopgap measures would
have helped!)

You can buy similar stuff at any hardware store. Strips of
sticky-backed foam, used for weatherstripping, usually comes in a roll
in a plastic bag. I believe some pinheads already have some around as a
replacement for the foam on older EM lockdown bars. Just cut a few
lengths of it and stick them in the gap. You might need to stick some
pieces on top of other pieces to block all the light, but you shouldn't
need more than a couple of inches total.

Duncan, thoroughly embarassed over that flaw

**14) More Info's about that tournament with servers:**
from RGP 7.8.2000

During discussion of the Ep1 game update to version 1.4 new discussions
came up about that tournament server used at pinball expo 2000.

Here a post from Tom Uban (one of the great developers):
You should just get Lyman to post the JAVA source for the tournament
server that he wrote for expo. We did all of the tournament work on
our own time, so I doubt that anyone would care.
--tom

Lyman: If you ever read this - think about this!! Martin

**15) Screen Problems during gameplay:**
from Harry W. 10/2000

During enforced gameplay sometimes the pics on screen looked like recorded
during an earthquake...
Answer: Reaseating the connectors to the computer-case (at least the 15 pin
VGA/CGA conector) resolved the problem!

**16) Pin2000 SW Update Problems:**

**A serial update Problem:**

Make sure that the filenames are EXACTLY as indicated by the source
files on the web site. If the update manager still can't find them,
move them to the "Pinball 2000 Update Manager" subdirectory under
Program Files\Pin2000 (assuming you installed in that directory).

Make sure you are communicating over COM1 port and don't have any other
program active that tries to grab the COM1 port.

If fupdate continues to fail, check your null modem cable and power
off/on the game after the game is connected to your laptop and try
again. Try not to have other DOS sessions open (I've had fupdate fail
with other DOS sessions open)

If all else fails, run FUPDATE directly:

fupdate -b115200 -debug COM1 (if I remember correctly, fupdate -? for
details)

Jim Hicks

**Another serial Problem:**
(RGP 10/00)
> I just brought one of my SWEP1 in for a general service and thought I would
> down load version 1.4 from the web. I down loaded the upgrade manager
> program and the revision 1.4 program and sound file combined. However when I
> went to configure the upgrade manager program it was locked in com port 2
> and wouldn't allow me to change to com port 1 and it would not allow me to
> scan disk for upgrade files.
> Anyone else had this problem? Any clues as to what is going wrong?????

This is likely due to some kind of problem with your computer's configuration
as I've also seen this happen ...

Check to see if things like Dial up Networking or modem drivers are "claiming"
a serial port and locking you out of it ...

As far as scanning the disk for updates, that's a normal behavior - after
you connect to the game, it'll allow you to scan the drive.
Jonathan

**17) Lock Down Bar:**

RGP 29.11.2000

Seems like Wms never made any spares for inventory or the distributors never
stocked them.
This is basically an angle iron with a slot for the lock that allows the
backglass on PB2000 to be locked or opened.
Please advise availability and cost or leads much appreciated.
Don L.
Lockdown Bars are now available at Pinball Life [Pinball Life RFM Parts](http://www.pinballlife.com/index.php?p=catalog&parent=150&pg=1)

**18) More Ethernet Things:**

Slash Dot Com 11.1.2001

Re:Technical details - here's how we did it (Score:5, Informative)
 by grahamwest on Thursday January 11, @06:56PM EST (#36)
 (User #30174 Info) http://www.midway.com/
 As one of the Pinball 2000 programmers, maybe I can shed some light.

Pin2000 uses PC-Xinu as the basis of its core OS although we added a lot of functionality to it. This was the decision of Tom
 Uban who was the chief software engineer on the project and all-round hardcore superstar programmer in general. PC-Xinu
 already includes a TCP/IP stack, and he had already written a packet driver for one kind of Ethernet card because we did all
 our code and image downloads via ethernet during development. A really simple web server wasn't too hard to write on top of
 that - all the statistics it reports are already collected by the game and displayed on-screen in the administration menus.

We demoed another use of the TCP/IP stack at Pinball Expo in 1999, where we had a tournament automatically running. We
 produced barcode badges for entrants, they walked up to a game, swiped the badge in a barcode reader, played, and their
 score was recorded. We also took their picture with a webcam and printed it on the badge, and the games showed the current
 high score list including their digitised pictures on all the games during their attract mode (ie. while they weren't being played).

Graham

**19) Pin2000 serial numbers:**

Some trivia about the serial numbers - Because Pinball 2000
 shipped in two separate containers (body and head) and the heads
 were universal (not country-specific), they decided to give them
 each their own serial number, in the normal random fashion...but
 all out of the same pool of numbers. So twice as many serial
 numbers got used up as there were games. This created all sorts
 of problems, but they hadn't come up with a solution by the time
 of the SW:EI MEL games. So my game, at 000051, is the 26th MEL
 game (my head has serial number 000050). But for production they
 came up with some other scheme - I believe they gave the heads
 numbers in a different range (the beginning digits were somehow
 different). I'm not sure what number they started on, could have
 been 000052, or it could have been 000100. No attempt was made
 to make the body and head serial numbers always be the same way
 odd/even, or to be sequential, so your head could have just about
 any number! (I believe John's game is head 000000/body 000001
 while Cameron's game was head 000003/body 000002 !) But don't
 quote me on those numbers - those were the games they had in their
 offices at work, I have no idea what games they ended up with at
 home. MEL means 'Made in Engineering Lab'.

**20) Pinball 2000 country specific differences:**

This information is very important for all doing a machine import!

The Pinball2000 tops are 100% identical all over the world - no country specific packages
The Pinball2000 bodys have been packed country specific with following differences known to me
- the coin door, e.g. german Pin2000 have an electronic Mars coin acceptor
- the transformer, which for worldmarket can be easily jumpered for different voltages
- the country DIP switch settings on the powerdriver set to e.g. Germany- mainly only for default language and coin setting
- the different power cord and main fuse (e.g. half value for 220V compared to 110V countries)
- the manual package, which might cointain translated parts

**21) Machine Differences between RFM and SW:E1******

There seem to be several little technical and mechanical differences between Starwars and Revenge.
Currently I found 2 of them, but there might be more.
1) There is a [Backbox connector](assets/backbox_connector.jpg) at SW:E1, which makes the game assembly much easier.
2) There is a clamp at the end of the SW:E1 playfield, which fixes the playfield when rised -
so that it do not lean against the backbox. Picture? (my MEL game does not have this)
