# XINA and Pinball 2000

> [!NOTE]
> Preserved from <https://www.gerwiki.de/xina/p2k> on `2026-04-20`. This Markdown is a readable derivative; `raw/` contains the captured evidence.

Quelle[^1]

XINA Background.

Pinball 2000 is controlled by a personal computer located in the head of the game. The Operating System controlling the PC is called XINA (which stands for It's Not APPLE, {Applied Pinball Programming Language Environment} where APPLE was the operating system used for WPC). XINA is unique in it's command structure (ordinary UNIX / DOS commands do not work). A total of 99 XINA commands are available to view, alter, and execute the parameters surrounding the game.

XINA is actually an application layer on top of PC-XINU, a multithreaded operating system originally created by Douglas Comer and documented in a very well known two volume set of books. (XINU information: public.ise.canberra.edu.au/~chrisc/xinu.html) PC-XINU won over other operating systems for a few reasons. For example, making Linux into a real-time system with thread support would involve modifying the kernel, which then obligated Williams into releasing the kernel modifications to the public under the GPL. WMS Legal didn't care much for that. Comer's XINU license was much more friendly to the lawyers; namely, we could do anything we wanted to it with no obligations.

XINU also turned out to be quite easy to turn into a real-time system, and in the end proved to be as easy as Linux or BSD to work with in an embedded application, if not easier.

It is much easier to interface to the game PC via a keyboard, rather than using the front-door panel (which only has 4 buttons). To hook up a keyboard, unlock and remove the backglass panel on the head. On the PC cage plug in a standard IBM PC/AT keyboard into the keyboard port (note that this PC has an old-fashion 5-pin DIN plug rather than the newer 6-pin mini-DIN plug). In most cases, the PC will detect the connection of the keyboard and place the machine into the SHELL mode. Also note that you can capture all printouts / command responses by connecting a PC to the serial port (9600 baud, 8, N, 1) and by having the PC run a terminal program (HyperTerminal). Typing HELP or ? will display the list of commands shown below.

Most XINA commands are common between RFM and SW:E1 games, though the XINA level in RFM 1.5 includes communication options and code that are not available in released levels of SW:E1 (it appears that much of the XINA code used on the SW:E1 during the tournament at Pinball Expo 99 was incorporated in XINA 1.17 in RFM 1.3/1.4). In addition there are some game-specific commands in RFM (there were apparently some cool game-specific commands for SW:E1 that were removed for production code build).

Many of the XINA commands available in RFM 1.5 are useful for advanced diagnostics or debugging of gameplay only and are of limited general use. During the development of a Pinball 2000 game, many such commands were added by the developers. Not all remain in the release level of code.

There are three commands that may be of general interest: fb, attack_mars, and scenemgr. Many others are interesting (e.g. pinevents), but these three are likely to be the most commonly used by the average RFM home owner.

Warning: DO NOT issue commands or alter settings using the XINA command set unless you know what you are doing and understand the implications. Damage can occur by using these commands recklessly.

XINA Function Key Commands.

```
F1 - Help
F2 - Flip Screen
F3 - Shell (toggle)
F4 - Start (same as Start button on Front-door)
F5 - Escape (same as Escape button on Front-door)
F6 - Down (same as Down button on Front-door)
F7 - Up (same as Up button on Front-door)
F8 - Enter (same as Enter button on Front-door)
XINA v1.18 Command List.
```

```
?
Attack_Mars
Audio
Bitmap
Bootdata
BPool
BS
Clear
CMOS_Buffer
CMOS
CONF
Continue
Credit
DCS
DeffMGR
DEVS
DGStat
DipSW
DispMGR
Diverter
Down
Drive
DropTGT
Dump
Echo
Enter
Errors
Escape
Ether
EventLOG
Exit
Fatal
FB

  Flags
FlapGate
FlipRamp
Flip
FUpdate
Game
GX
Help
History
HSTD
HTTPD
IFStat
IGMP
Info
KEvents
Kill
LampMGR
Lamp
LeffMGR
LockPost
Loops
Martians
MEM
Midas
MON
Multi
NetStat
Net
NonFatal
NSLookup
PAL
PDB
PinEvents

  Ping
Pool
Price_Current
Price_Dyn
Price_Table
PrintOut
PS
PTY
Pub
Queue
Ramps
RASYS
Reboot
Replay
Reslist
Resources
Routes
Route
RTC
SceneMGR
SEM
Sleep
Stack
Start
Switch
Term
TimeRQ
Time
UpdtMGR
Up
VDAI
ZC
Zombie
```

   XINA Command List Usage.
 (thanks to Jim Hicks for his contributions to this section)

 Typing the command (with no other text) will display the proper usage of the command, parameters, acceptable values, etc. Some exceptions to this are obvious, such as commands like HELP, REBOOT, etc. Commands/arguments should be typed in all lowercase. (They are displayed here in upper/lower case for easier reading). If a command's function is not obvious in what it performs, the listing below will say "(function unknown)" to indicate that I do not know what the command does.

 Syntax: A <parameter> is manditory, <param1 | param2> means one of param1 or param2 is mandatory. A [parameter] is optional, [param1 | param2] means either of param1 or param2 is optional. A 'N' means a numeric value, 0 to ?, based on the command.
XINA v1.18 Command List Usage.

```
?    Produces a listing of commands, same as "HELP"
   Usage:
   ?

  ATTACK_MARS    Starts / stops the Attack_Mars mode.  Can be used to start Attack Mars mode for practice.
  Usage:
   attack_mars < Start | Stop | FlagRamps | FlagMS | FlagModes >

  AUDIO
   Adjusts audio properties
   Usage:
   AUDIO INIT
   AUDIO INFO
   AUDIO Quiet {0-5}
   AUDIO Volmin
   AUDIO Voldef
   AUDIO Volmax
   AUDIO Vol++
   AUDIO Vol--

  BITMAP    Displays bitmap memory allocations
  Usage:
   bitmap < Info | Main | Waste >

  BOOTDATA    Change / view which boot image is booted
  Usage:
   bootdata < Current | ROM | Flash > [Verify]

   BPOOL    (function unknown)
   Usage:
   bpool

   BS    (function unknown)
   Usage:
   bs < debugon | debugoff | off | on >

   CLEAR    (function unknown)
   Usage:
   clear < cmos >

    CMOS_BUFFER    Displays flash CMOS memory utilization
   Usage:
   cmos_buffer < view | headers | reset | off >

   CMOS    Displays ROM CMOS memory utilization
   Usage:
   cmos < view | headers | flash | prologue | reset | off >

   CONF    Provides XINA status / table information
   Usage:
   conf

    CONTINUE    Used internally for continuing programming scripts
   Usage:
   continue

    CREDIT    Manages current credits
   Usage:
   credit < init | info | dec >

   DCS    Manages the digital sound system
   Usage:
   dcs N [track [pan]]
   dcs < + | - >
   dcs raw <N>
   dcs quiet [track]
   dcs trkvol <trk vol>
   dcs trkpan <trk pan>
   dcs signals [clear]
   dcs version
   dcs [warm] reset

   DEFFMGR    (function unknown)
   Usage:
   deffmgr < list[_N] | debug<on|off> | log<on|off> | entry<addr> | names[_N] | unreq<addr> >

   DEVS    Displays a listing of device names
   Usage:
   devs
   DGSTAT    (function unknown)
   Usage:
   dgstat

   DIPSW    Shows the dip switch value
   Usage:
   dipsw

   DISPMGR    (function unknown)
  Usage:
   dispmgr < list[_N]debug<on|off> | entry<addr> | frames | fclear | locks[_N] | log<on|off> | on | off >

    DIVERTER    Shows info regarding the diverter (ramp)
   Usage:
   diverter < debugon | debugoff | info >

   DOWN    Same as the Down button on the Front-door
   Usage:
   down

   DRIVE    (function unknown)
   Usage:
   drive < 0-47 | list | off | highon | highoff >

   DROPTGT    Status of right ramp drop target
   Usage:
   droptgt < debugon | debugoff | info >

   DUMP    Provides a memory dump
   Usage:
   dump <address> [count]

   ECHO    Allows a command to be echoed to the window / log
   Usage:
   echo

   ENTER    Same as the Enter button on the Front-door
   Usage:
   enter

   ERRORS    Displays a summary of known errors
   Usage:
   errors

   ESCAPE    Same as the Escape button on the Front-door
   Usage:
   escape

   ETHER    Displays Ethernet network statistics
   Usage:
   ether < info >

    EVENTLOG     Manages the event log buffers
   Usage:
   eventlog < dump[depth[count]] | flush | stats | types >

   EXIT    Exit a running script
   Usage:
   exit

   FATAL    Displays a listing of the fatal error log
   Usage:
   fatal

   FB    Monitor adjustments, i.e. flips display.  Useful for flipping the monitor when the playfield glass is removed.
   Usage:
   fb < clear | bars | border | pillars | vsyncs | flip | sync V(0:1) H(0:1) >

   FLAGS    Displays flag values / state
   Usage:
   flags < local | global | static >

    FLAPGATE    Monitor status of left / right flap gates
   Usage:
   flapgate < debugon | debugoff | info >

    FLIPRAMP    Monitor status of up / down ramp
   Usage:
   flipramp < debugon | debugoff | info >

   FLIP    Toggle state of flippers
   Usage:
   flip disable [all|0-3] | enable[all|0-3] | player<all|0-3> | computer<all|0-3> | on<all | 0-3> | off <all | 0-3>| debug >

   FUPDATE     Firmware Update
   Usage:
   fupdate < load<com1 | com2> <baudrate> [sf | sfonly] >
   fupdate enable
   fupdate disable

   GAME    Change / view game parameters.
   Usage:
   game < info | tilt | over | name | collect >

   GX    Displays various GX processor configuration registers
   Usage:
   gx < crs | ccrs | bcrs | mcrs | dcrs | id | cx5520 >

   HELP    Produces listing of available commands, same as "?"
   Usage:
   help

   HISTORY    Shows last commands entered from the keyboard
   Usage:
   history

   HSTD    Displays High Score To-Date Tables
   Usage:
   hstd

   HTTPD    Displays Internet hyperlink values
   Usage:
   httpd < list | stats >

   IFSTAT    Switches between interface 0 or 1
   Usage:
   ifstat < intf >

   IGMP    Connect / disconnect via network to other machines
   Usage:
   igmp < join | leave> <group>

   INFO    (function unknown)
   Usage:
   info

   KEVENTS    Toggle values of key event settings
   Usage:
   kevents resource pid <on | off>
   kevents proc_create <on | off>
   kevents proc_suicide <on | off>
   kevents proc_kill <on | off>
   kevents proc_reap <on | off>
   kevents sem_create <on | off>
   kevents sem_delete <on | off>
   kevents hook_exec <on | off>

   KILL    Terminate a process
   Usage:
   kill < process-id >

   LAMPMGR    Monitor the lamp matrix
   Usage:
   lampmgt < list[_n] | matrixes[_n] | debug<on|off> | log<on|off> | on | off >

   LAMP    Perform lamp tests, change lamp settings
   Usage:
   lamp test
   lamp lamp <all | N> <duty>
   lamp blink <all | N> <duty>
   lamp blink rate <time>
   lamp effect <all | N> <duty>
   lamp mask <all | N> <duty>
   lamp dump <lamp | blink | effect | mask>
   lamp saver <on | off | on_time N | off_time N>

   LEFFMGR    (function unknown)
   Usage:
   leffmgr < list[_n] | debug<on|off> | log<on|off> | unreq <addr> | names[_n] >

    LOCKPOST    Monitor the jet bumper exit post
   Usage:
   lockpost < debugon | debugoff | info >

   LOOPS    (function unknown)
   Usage:
   loops < debugon | debugoff | info >

    MARTIANS    Displays parameters about the two bouncing Martians
   Usage:
   martians < debugon | debugoff | info >

   MEM     Displays current memory utilization
   Usage:
   mem < stat | free | usage <PID> | allocs [entries] | allocs_d [ entries [size]] >

   MIDAS
   (function unknown)
   Usage:
   midas <info | enable | disable>
   midas monitor [on | off]
   midas debug_level [N]
   midas send <ack | nack | rvi | eot>
   midas send cash door <open | close>
   midas send cash <5p | 10p | 20p | 50p>
   midas send cash <L1 | L2 | L5 | L10>
   midas send token <5p | 10p | 20p | 50p>
   midas send token <L1 | L2 | L5 | L10>
   midas send hstd <N>

   MON    Reboots the machine with an extensive log displayed
   Usage:
   mon

   MULTI    Provides current status of various flags
   Usage:
   multi < list [N] >

    NETSTAT    Displays the current network status
   Usage:
   netstat

   NET    Control / monitor the Ethernet network capability
   Usage:
   net < start | monitor [mac] <on | off> | nif <N> >
   Example:
   net monitor 0x00 0xe0 0x29 0x0f 0xeb 0xf6 on

    NONFATAL    Displays the non-fatal error log
   Usage:
   nonfatal

    NSLOOKUP    Network Service Name Lookup
   Usage:
   nslookup

   PAL    (function unknown)
   Usage:
   pal < on | off >

   PDB    Displays current status of the Power Driver Board
   Usage:
   pdb

    PINEVENTS    Triggers a multitude of events
   Usage:
   pinevents game <on | off> all
   pinevents game <on | off> game_init
   pinevents game <on | off> add_player
   pinevents game <on | off> cycle_players
   pinevents game <on | off> game_start
   pinevents game <on | off> game_restart_game
   pinevents game <on | off> game_restart_ball
   pinevents game <on | off> ball_start
   pinevents game <on | off> first_ball
   pinevents game <on | off> ball_serve
   pinevents game <on | off> ball_serve_state
   pinevents game <on | off> outhole_made
   pinevents game <on | off> valid_playfield
   pinevents game <on | off> score
   pinevents game <on | off> endball_begin
   pinevents game <on | off> endball_kill
   pinevents game <on | off> game_over
   pinevents game <on | off> bonus
   pinevents game <on | off> match
   pinevents multi <on | off> all
   pinevents multi <on | off> audit_complete
   pinevents btime <on | off> all
   pinevents btime <on | off> ball_search
   pinevents btime <on | off> status_report
   pinevents btime <on | off> chase_ball
   pinevents tilt <on | off> plumb
   pinevents tilt <on | off> ball_roll
   pinevents tilt <on | off> game
   pinevents tilt <on | off> slam
   pinevents update <on | off> request
   pinevents update <on | off> update
   pinevents update <on | off> group
   pinevents update <on | off> object
   pinevents update <on | off> delay_pid

   PING    Determine if a link / host is available
   Usage:
   ping host [size]

   POOL    (function unknown)
   Usage:
   pool < stat >

    PRICE_CURRENT    Displays the current value of coins
  Usage:
   price_current

    PRICE_DYN         Displays status of dynamic (multiple coins) pricing
   Usage:
   price_dyn

    PRICE_TABLE    Displays the price / coin table
   Usage:
   price_table

    PRINTOUT          Displays via shell various printouts / audit logs
   Usage:
   printout < audits | adjustments | hstd | hourly | daily | pricing | fatal | nonfatal | everything >

  PS   Displays CPU processes in memory
   Usage:
   ps

   PTY    Check status of COM / Printer port
   Usage:
   pty < stat [N] >

   PUB    (function unknown)
   Usage:
   pub < game | sound1 | sound8> dump <address> [<count>] >

   QUEUE     (function unknown)
   Usage:
   queue < sleep | ready >

   RAMPS    (function unknown)
   Usage:
   ramps < debugon | debugoff | info >

   RASYS    (function unknown)
   Usage:
   rasys < debugon | debugoff >

   REBOOT    Reboots (re-starts) the machine, smaller log entries
  Usage:
   reboot

    REPLAY    Display / change replay values
   Usage:
   replay <info | buckets | check | reset>
  replay add N [random | low | middle | high] [debug]
  replay boost <ah | ar | coin | gs | go | rf>

    RESLIST     Displays a lengthy resource manager / system list
   Usage:
   reslist

   RESOURCES     Displays a list of resources in use
   Usage:
   resources

   ROUTES    Displays current network routing information
   Usage:
   routes

   ROUTE    Configure network routing information
   Usage:
   route add <dest> <mask> <gateway> <metric> <ttl>
  route delete <dest> <mask>

   RTC    Access various RTC chip registers
   Usage:
   rtc < dump >

   SCENEMGR    Manage the 12+ scenes (modes) (including bonus waves)
   Usage:
   scenemgr < awardshot<id> | awardswitch<id> | debugoff | debugon | info | reset<id> | resetall |  select<id>| start<id> | stopall >
     Notes: the scenemgr command can be used to manage which scene is active in the game and/or display information on the status of an active scene.  The command can be used be used at the beginning of the game to select the starting scene, including selecting scenes in the middle group that are not normally selectable by the action buttons.  For example, use "scenemgr select 8" to select "Drive-In Demolition" as your first scene.  All other gameplay continues options/rules continue normally.

 During gameplay scenemgr can be used to start another scene at anytime using the format:

 scenemgr resetall stopall start N            where N =

  0 - "Fuel Bonus Wave"
  1 - "Saucers Bonus Wave"
  2 - "Weapons Bonus Wave"
  3 - "Alien Abduction"
  4 - "Martian Happy Hour"
  5 - "Secret Weapon"
  6 - "Tower Struggle"
  7 - "Question Mark"
  8 - "Drive-In Demolition"
  9 - "Paris In Peril"
 10 - "Big-O-Beam"
 11 - "Mars Kneads Women"

   SEM    Shows status of semaphores
   Usage:
   sem

   SLEEP    Pause execution of program by delay factor
   Usage:
   sleep < delay >

   STACK    (function unknown)
   Usage:
   stack < history >

   START    Same as the Start button on the Front-door
   Usage:
   start

   SWITCH    Perform switch diagnostics
   Usage:
   switch < debug | callbacks | timers | counters >
  switch test < on | off >
  switch trace < on | off >
  switch break number

   TERM     Manage the serial (terminal) port
   Usage:
   term < on | off | capslock | control | swap >

   TIMERQ    (function unknown)
   Usage:
   timerq

   TIME    Displays the current date and time
   Usage:
   time

   UPDTMGR    Manages scenes, backgrounds, sounds, music
   Usage:
   updtmgr < list | update >

   UP    Same as the Up button on the Front-door
   Usage:
   up

   VDAI    (function unknown)
   Usage:
   vdai info

   ZC    (function unknown)
   Usage:
   zc

   ZOMBIE    (function unknown)
   Usage:
   zombie process-id
```

### Networking P2k Games: Adding an Ethernet Card.
 Why add an Ethernet card to your pinball 2000 game? Good question! Maybe because you saw the SW:E1 machines network-linked together at Pinball Expo '99. Maybe because with an Ethernet card installed you can access your RFM's internal commands via the Internet (to check earnings, diagnostics, etc). Or perhaps you own more than one PB2K machine and you're going to have your own tournament. To tell the truth I have not installed an Ethernet card in my machine, but I wanted to document the procedure as I know it. Again, perform this kind of operation at your own risk.

#### Ethernet Card Specifications
 I've been told that RFM only supports one brand of Ethernet Card, an SMC (brand) ETHEREZ 16BIT ISA 10MBPS RJ-45 network card (mfg part# SMC8416T, typically goes for $31 USD at places like www.buy.com, but this card is becoming increasing difficult to find). This network card needs to be installed into the extra ISA slot in the RFM (v1.4 or better) pinball machine motherboard. After the settings are configured for the network adjustments, you can connect to the XINA command shell via telnet or alternately you can connect to the machine using a web browser (a simple httpd server is built in!).
#### Ethernet Configuration Instructions.
 Note that depending on the motherboard in the RFM machine, you may have to adjust the BIOS setup setting prior to getting the network card working in order to avoid an IRQ conflict. This can be determined if there is a problem by configuring the network adjustments, setting an IP address/mask, and then using the keyboard (or serial port) to access a command prompt and run 'ping'.  If the 'ping' works (the lights on the card blink or device on the network responds to the ping, etc.), then you don't need to change the BIOS. If you get some kind of "ez" error message, then you will have to do the following:
 Remove the PRISM board, plug in a keyboard and power up the box. If you are lucky and the monitor "syncs up" to the BIOS screen, then you can see what you are doing. If not, you may want to temporarily plug in a PC monitor instead of the Pin2000 monitor.
 Disable the 'Built in OnBoard Audio' in the 'Integrated Peripherals' menu. On some RFM motherboards, the onboard audio uses the IRQ that the default setting of the EtherEZ network card uses.
 Here are the key strokes that should effect the change:

```
Remove the PRISM board/plug in PC monitor and keyboard
Power up (you will see the normal PC BIOS startup because the PRISM card is out).
Press DEL to enter setup (the BIOS setup should appear).
Press the RIGHT ARROW key (move to the INTEGRATED PERIPHERALS menu).
Press the ENTER key (enter the INTEGRATED PERIPHERALS menu).
Press the RIGHT ARROW key (move to the right hand column of menu items).
Press the DOWN ARROW key (move to the BUILT IN ONBOARD AUDIO menu).
Press the PAGE UP key (change the adjustment from ENABLE to DISABLE).
Press the ESC key (leave the INTEGRATED PERIPHERALS menu).
Press the DOWN ARROW key (move to the SAVE AND EXIT menu).
Press the DOWN ARROW key.
Press the DOWN ARROW key.
Press the ENTER key (SAVE AND EXIT).
Press the Y key (confirm).
Press the ENTER key (do it).
```

#### Replace the PRISM board.
 The default username / password are set to "Pin2000" and "Manager" (respectively, case sensitive), which you may want to change since all RFM machines are initially set to the same values (but who would want to hack into a pinball machine?)
Wireless Access to your Pinball 2000 Game.

 Matt Osborn came up with this info. If your basement arcade is anything like mine, you don't have outlets all wired up to the home network, and snaking CAT5 isn't going to be much fun. There's a simpler and easier way to get connected. But before we get to that, you're going to need to get the right network card for your Pinball 2000 game. Apparently, Williams only had time to add support for one specific ethernet card, the SMC8416T. You'll have to hunt around to find one.

 What you need is a wireless bridge. Logitech used to make one called the "Play Link", which was marketed for network capable consoles link PS2 and XBox. Like just about everything good for Pin2000, it's no longer made. I was fortunate enough to pick one up when they were on clearance at geeks.com for $9.99 (wish I'd bought more than one!) You see used ones pop up on eBay fairly often. There are probably other bridges out there, but the Play Link is inexpensive, simple, and works great. All you have to do is run the provided network cable from your P2K network card to one of the Play Link cubes, and connect the other cube to your router. The two cubes automatically make an RF connection. There's no configuration to do. It's essentially a "wireless cable" from your pinball to the router. I run the network cable down the back of the machine (along with the other cables, like the parallel cable that goes to the driver board) and put the cube down in the bottom of the cabinet. The cube has a little wall-wart power supply that you can plug into the utility power outlet with the help of the adapter cord that comes with the machine.

 The Logitech PlayLink installed in the bottom of a RFM machine. Picture by Matt Osborn.

   In the P2K setup you'll need to assign an IP address to your machine. In a home networking environment that'll be something like 192.168.1.X (assuming your router's IP is 192.168.1.1), where "X" can be any number from 2 to 255. Don't pick too low of a number, as those addresses are probably already used by other computers on your net (likely automatically assigned by DHCP). To test it out, the first thing to try is the machine's webserver, which is http://192.168.1.X web address. If everything is set up right you'll get a webpage that looks something like this:

 The web page at http://192.168.1.X ('X' is the number you assigned),
 showing high scores.

----

pinballz.net-Diskussion: [^2]

Hiho,

 so wie versprochen hier ein kleines update mein wie ich mein RfM über das netz zum server von Jim Askey www.mypinballs.com verlinkt habe.

 Jim hat ein system entwickelt welches sich an dem von der pinball expo 1999 anlehnt. d.h. spieler können sich per barcodekarte am jeweiligen RfM anmelden und ein turnierspiel machen. nach abschluss des spiels wird der erspielte score per internet an den server gesendet und in einer öffentlichen liste angezeigt. nach einer software änderung (hab ich noch nicht gemacht - kommt noch) kann auch die hs-list auf dem RfM angezeigt werden. eine ganz lustige geschichte wie ich meine...

 um das alles machen zu können sind einige vorbereitungen nötig:

 1) SMC 8415T Ehternetkarte im rechner einbauen (es geht nur diese ethernetkarte keine andere!)
 eventl. im BIOS onboard sound ausschalten. ich habe gehört das bei manchen rechner es sonst zum irq clash kommt. übrigens Karte hab ich über ebay für ca. 5 euro +v und p bekommen.

 2) Linksys wrt54gl router besorgen (Altelco ca. 60 Euro). diesen mit einer neuen open source firmware flashen (DD-WRT V23 SP2 s. www.dd-wrt.com ). dies notwendig da später einige commands hinzugefügt werden müssen.

 soweit so gut...
 aus meiner sicht wird es dann ein wenig "fummelig". der wrt router muss als (wireless) client an den hauptrouter angeschlossen werden. hierzu gibt es tutorial via Jims HP. wichtig ist hierbei das richtige IP-setup! danach werden die o.g. kommandos in die firewall gespeichert. bei mir war etwas schwierig den RfM in der LAN-liste des WRT zu finden. daher habe ich den DHCP auf maximal 2 ip´s beschränkt - somit wusste ich welche ip der RfM hat. dies lies sich dann auch per ping bestätigen. wichtig ist auch die portfreigabe (port 2069 udp und tcp) auf beiden routern. leider funzte es bei meinen system nicht sofort aber nach umfangreicher hilfe von Jim (er hatte sich zeitweilig auf mein system eingeloggt) klappte es dann gestern erstmalig und ich konnte meinen ersten score in der onlineliste verbuchen. ach ja, natürlich muss auch das game setup unter communications ergänzt werden. auch muss tounament play aktiviert werden.

 da ich noch keinen barcode reader installiert habe (s. thread: Suche Barcode Slotreader RS232 f. RfM ) kann ich z. Zt. nur über meiner ip adresse spielen. dies ist etwas lästig da ich jedesmal meine externe ip im profil ändern muss (mein isp vergibt keine statischen ips).

 unterm strich alles ziemlich aufwendig - macht aber spaß. ausserdem steckt die ganze sache noch in den "kinderschuhen" wird aber sicherlich wachsen. deswegen um so mehr mitmachen umso mehr spaß.

 hier auch nochmals ein großes "thanks Jim" für seine hilfe und den willen ein solche sache auf die beine zu stellen.

 Gruß

 Hardy
 P.S. sofern jemand hilfe benötigt und ich als nicht compi futzi helfen kann einfach eine PM oder hier im thread.

Gerald:

Mal einige doofe Fragen:

Wozu braucht man denn den Linksys-WRT-Router? Warum kann man denn den Flipper nicht direkt an den DSL-Router hängen und dort die Portfreigaben machen? Was heisst, dass 'da später einige commands hinzugefügt werden müssen' und warum?

 Weiss das jemand oder kann mir jemand sagen, wo man die technischen Hintergründe nachlesen kann????
 Danke!

hallo gerald,

 meistens ist der home router nicht in der nähe des pins. dadurch hast du sozusagen einen wireless anschluss für den RfM.

 den wrt braucht man um eine kommandozeile in die firewall zu programmieren (ich glaube linux) und nicht jeder router hat diese schnittstelle. die kommendos beziehen sich auf die packetgroße die angepasst werden muss.

 ich kann wir durchaus vorstellen das es eventl. andere router gibt die das auch können - aber welche Confused

 gruß

 hardy

Hi hardy,

 danke für die schnelle Antwort. Das ergibt Sinn.
 Wireless ist zumindest bei mir kein Problem, da ich Strippen durch ganz Haus gezogen habe. Lediglich am funktionierenden RFM scheitert es im Moment (steht zerlegt in der Ecke).
 Mit der weit verbreiteten Fritzbox sollte es auch funktionieren. IMHO kann man da zwar die Paketgrösse nicht von Haus aus für jedes angeschlossene Gerät einstellen. Die Fritzboxen basieren jedoch komplett auf Linux und GPL sei dank muss AVM alle Quellcodes offenlegen. Es gibt dafür auch zahlreiche Community-Firmwares und viele Plugins. Außerdem kann man sich eh per Telnet in ne Fritzbox einwählen und dort Befehle absetzen - allein das könnte schon reichen.
 Weisst du denn, welche Kommandos man für den Linksys braucht???

ja, wäre mal interessant ob es mit Fb geht.

 der befehl:

```
iptables -t mangle -I PREROUTING -i br0 -s XXX.XXX.XXX.XXX -j TTL --ttl-set 64
```

 wobei XXX.XXX.XXX.XXX die ip vom RfM sein sollte.

 Hardy

 p.s. laut dd-wrt läuft die nicht auf der Fb. aber gibt bestimmt was anderes.

Gerald:

Hm, soweit ich das verstehe macht der Befehl nur folgendes:
 Der Router (kann aber auch jeder beliebige Linux-Rechner mit 2 Netzwerkschnittstellen sein) nimmt ein IP-Paket, das vom Flipper kommt und modifiziert den IP-Header und schickt es weiter.
 Wenn ein IP-Paket eine maximale Anzahl von von Hops (Stationen) zum Server passiert hat, wird es gelöscht. Das heisst TTL (Time to live). Vermutlich stellt der Flipper nur eine sehr geringe Zahl von TTLs ein, da die Software ja ursprünglich nur fürs LAN gedacht ist; Fürs Internet mit den vielen Zwischenstationen zum Server viel zu wenig. Der Befehl setzt die max. Hops auf 64. Das dürfte auch im hintersten Afghanistan reichen.
 Müsste man man überlegen, wie man das einfacher verstellen könnte. Am einfachsten natürlich: Firmware des RFM patchen (ist ja nur eine Zahl)

 Interessant ist auch, dass die RFMs -meines Wissens nach- nicht per TCP, sondern per UDP mit dem Server kommunizieren. UDP ist ein verbindungsloses Protokoll. Wenn ein Paket verworfen wird (etwa wegen zu gering eingestellter TTL), gibt es keinen weiteren Sendeversuchs dieses Pakets.

#### PUB Prism Update Board
Update via PUB-Karte:

![](assets/pub_card_user_guide.odt)

[^1]: http://www.pinrepair.com/pin2000/xina.htm
[^2]: http://www.pinballz.net/thread.php?threadid=789
