# myPinballs Revenge From Mars update help

> [!NOTE]
> Preserved from <https://mypinballs.com/software/rfm/update_help.jsp> on `2026-11-11`. This Markdown is a readable derivative; `raw/` contains the captured evidence.

**Notes for updating pinbox systems**

To get to the terminal and or desktop of the system from a running game (and not wanting to use ssh,sftp) do this first:

1. Press F1
1. Log in from the command prompt (if its a system i setup contact me via email for the details). Use your own details if not
1. for gui desktop type sudo lxdm , or use the terminal

Otherwise start here:

1. On a computer of your choice extract the files within the update exe (This is a basic zip file) There are 6 files:

  1. pin2000_50070_0200_bootdata.rom
  1. pin2000_50070_0200_game.rom
  1. pin2000_50070_0200_im_flsh0.rom
  1. pin2000_50070_0200_pubboot.rom
  1. pin2000_50070_0200_sf.rom
  1. pin2000_50070_0200_symbols.rom

1. Copy the files to a spare usb drive
1. On your rfm pinbox computer, Navigate to folder **/root/pinbox/update/rfm_15/** (either by logging into lxdm, or from command line, or using ssh/sftp etc)
1. Delete or move ALL the existing files within this folder
1. Copy the new files to this folder. Only the files, no additional folders.
1. Make sure the update.bin file from the original installation of pinbox is deleted/moved. This will be recreated from the new update files
1. Move/delete rfm_15.nvram* files from **/root/pinbox/roms/savedata/** These files store the settings and high score data but may stop the update if not deleted first.
1. Reboot the rfm computer
1. Enjoy v2.0 game code on pinbox
