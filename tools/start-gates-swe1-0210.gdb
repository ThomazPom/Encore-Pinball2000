# Read-only Start eligibility trace for SWE1 2.10.
#
# Addresses come from pin2000_50069_0210_symbols.rom and objdump of the
# matching game ROM.  QEMU's GDB stub owns the breakpoints: this script does
# not patch guest RAM or change any register.  Every command list ends in
# continue so the graphical guest only pauses long enough to print one line.
# It is still stop-the-world observation: use deliberate, spaced Start presses.
# A long key-mash can accumulate enough debugger pauses to trip XINA's exec
# watchdog and is not evidence of an uninstrumented emulator crash.

set pagination off
set confirm off
set verbose off
set disassembly-flavor intel
set logging overwrite on
set logging redirect off

set $start_seq = 0

# swd_start_button_proc(): one hit per native Start closing edge.
break *0x00138dac
commands
  silent
  set $start_seq = $start_seq + 1
  printf "\nSTART #%u: native closing edge\n", $start_seq
  continue
end

# hstd_is_active() has just returned.  An active initials editor diverts Start
# before Game::m_credit_button_pressed(), so it is reported as routing context
# rather than one of the seven new-game checks.
break *0x00138db4
commands
  silent
  if $eax == 1
    printf "  route       REJECT new game (high-score initials owns Start)\n"
  else
    printf "  route       game-start path\n"
  end
  continue
end

# 1. menu_credit_button_pressed(): 1 means the menu consumed Start.
break *0x001cfb3b
commands
  silent
  if $eax == 1
    printf "  1 menu      REJECT (consumed)\n"
  else
    printf "  1 menu      PASS\n"
  end
  continue
end

# 2. SwitchActiveTest(Game::slam_tilt_switch): 1 means Slam Tilt is active.
break *0x001cfb4f
commands
  silent
  if $eax == 1
    printf "  2 slam      REJECT (Slam Tilt active)\n"
  else
    printf "  2 slam      PASS\n"
  end
  continue
end

# 3. Game::m_credit_start_is_ok(): nonzero means Free Play or credits and the
# game hook both permit a start.
break *0x001cfb65
commands
  silent
  if $eax == 0
    printf "  3 credit    REJECT (no credit/free play or credit hook)\n"
  else
    printf "  3 credit    PASS\n"
  end
  continue
end

# 4. AudioIsReady().
break *0x001cfba7
commands
  silent
  if $eax == 0
    printf "  4 audio     REJECT (audio not ready)\n"
  else
    printf "  4 audio     PASS\n"
  end
  continue
end

# MultiDevice detail: active generic device process range.
break *0x001c7a43
commands
  silent
  if $eax == 1
    printf "      devices detail: process first_multi_device..last_multi_device active\n"
  end
  continue
end

# MultiDevice detail: active multi_kick_start process.
break *0x001c7a52
commands
  silent
  if $eax == 1
    printf "      devices detail: process multi_kick_start active\n"
  end
  continue
end

# MultiDevice detail: current registered device's virtual start check.
break *0x001c7a7e
commands
  silent
  if $eax == 0
    if $ebx == 0x002e2c60
      printf "      devices detail: trough_eject (my_trough_eject) rejected\n"
    else
      printf "      devices detail: device 0x%08x rejected\n", $ebx
    end
  end
  continue
end

# MultiDevice detail: ball audit result and its four returned counters.
break *0x001c7aa1
commands
  silent
  if $eax == 0
    printf "      devices detail: ball audit short; missing=%u count-a=%u count-b=%u count-c=%u\n", *(unsigned int *)($ebp-4), *(unsigned int *)($ebp-8), *(unsigned int *)($ebp-12), *(unsigned int *)($ebp-16)
  end
  continue
end

# MultiDevice detail: missing-ball policy after an incomplete audit.
break *0x001c7ab3
commands
  silent
  if $eax == 0
    printf "      devices detail: missing-ball policy REJECT (search/delay remains)\n"
  else
    printf "      devices detail: missing-ball policy permits degraded start\n"
  end
  continue
end

# 5. MultiDevice::game_start_check().
break *0x001cfbb4
commands
  silent
  if $eax == 0
    printf "  5 devices   REJECT\n"
  else
    printf "  5 devices   PASS\n"
  end
  continue
end

# 6. jts_game_start_check().  It should pass for a new game while game_over is
# true; retaining it here proves that assumption during the live run.
break *0x001cfbc1
commands
  silent
  if $eax == 0
    printf "  6 JTS       REJECT\n"
  else
    printf "  6 JTS       PASS\n"
  end
  continue
end

# 7. Xina::hook_game_start_check(), reached through the Game vtable.  SWE1
# returns zero while pid_recent_game_over_kickout (0xd1) exists.
break *0x001cfbde
commands
  silent
  if $eax == 0
    printf "  7 XINA      REJECT (pid_recent_game_over_kickout active)\n"
  else
    printf "  7 XINA      PASS\n"
  end
  continue
end

# Positive terminal evidence.
break *0x001d2480
commands
  silent
  printf "  RESULT      ACCEPTED -> Game::m_start_a_game()\n"
  continue
end

set logging enabled on
printf "Start trace armed for SWE1 2.10; guest resumed.\n"
continue
