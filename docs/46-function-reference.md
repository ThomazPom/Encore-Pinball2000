# 46 — Key Function Reference (Game Binary)

This is a curated reference of the most useful functions discovered
in the original Pinball 2000 (P2K) game binary during reverse
engineering. It is provided as a navigation aid for contributors who
are exploring the disassembly of the original runtime; the names
shown are the analyst-assigned labels, not symbols present in the
shipped binary (P2K binaries are stripped).

Line numbers refer to the decompiled C reconstruction maintained
in the upstream research fork; they will not match anything in
Encore's own `src/` tree.

| Function name                              | Lines        | Category    | Description |
|--------------------------------------------|--------------|-------------|-------------|
| `entry()`                                  | 5913–5919    | Boot        | ELF entry point. |
| `executeMainFlow()`                        | 36654–36818 | Boot        | Real `main` — orchestrates everything. |
| `initializeCpuAndDevices()`                | 38498–38581 | Emulator    | x86 CPU + all device init. |
| `loadSystemBIOSFromFile()`                 | 38428–38493 | ROM         | BIOS loading + memory registration. |
| `loadConfigurationData()`                  | 17260–17400 | ROM         | U100–U107 ROM chip loading (interleaved). |
| `initializePlayfieldAndSetRomPath()`       | 8780–8889   | ROM         | ROM file path setup. |
| `readAndInterleaveFileData()`              | —            | ROM         | Even/odd byte interleaving. |
| `loadRomData()`                            | 8900–8921   | ROM         | Master ROM loader. |
| `initializeVideoBIOS()`                    | 8935–8975   | ROM         | Video BIOS 32 KB load. |
| `processGameDataFiles()`                   | 12437–12506 | Update      | Update ROM concatenation and loading. |
| `initializeRuntimeSystem()`                | 8484–8520   | Init        | Runtime IPL — memory, semaphore, LEDs. |
| `initializeGameMemory()`                   | 7764–7788   | Init        | Zero all key memory regions. |
| `initializeGameResources()`                | 7806–7830   | Init        | Default paths and sound settings. |
| `initializeSDLVideoSettings()`             | 30642–30710 | Display     | 640×480 SDL setup. |
| `initializeSdlMixerAndLoadSampleInfo()`    | 19371–19410 | Audio       | SDL_mixer + sample loading. |
| `loadAndValidatePB2KConfiguration()`       | 26880–26970 | Config      | `pb2k.cfg` parser. |
| `initializeWatchdogSystem_1()`             | 13120–13160 | Watchdog    | Watchdog + shared-memory init. |
| `detectAndConfigurePlayfield()`            | 8710–8780   | Hardware    | Parallel-port playfield auto-detect. |
| `validateRootAccessAndMultiCoreSupport()`  | 8430–8470   | Init        | Root + SMP check. |
| `isStringXINACMOS()`                       | 6105–6111   | XINA        | CMOS signature validation. |
| `handleSystemReset()`                      | 8530–8555   | Runtime     | Full system reset. |
| `shutdownRuntimeSystem()`                  | 8560–8610   | Runtime     | Graceful shutdown. |
| `notifyInvalidGameName()`                  | 8630–8660   | Init        | Lists valid games: `rfm_15`, `swe1_14`, `auto`. |
| `printPciDeviceInfo()`                     | 40252–40310 | PCI         | Debug: print PCI device info. |
| `initializeDeviceConfiguration_1()`        | 39200–39225 | PCI         | i440FX host-bridge init. |
| `configureAndInitializeDevice()`           | 39498–39527 | PCI         | PIIX3 south-bridge init. |
| `processUsbDrive()`                        | 12680–12700 | Update      | USB update-drive handling. |
| `loadSoundLibrary()`                       | 19133–19180 | Audio       | Sound library loading (XOR `0x3A` decode). |
| `loadWavFile()`                            | 18769–18841 | Audio       | WAV sample loading from sound library. |
| `processByteArrayWithConstants()`          | 11952–11975 | Obfuscation | String decode (`^0xAB - 0x4D`). |
| `retrieveConstantValue_1()`                | 11914        | Obfuscation | Returns `0x4D`. |
| `retrieveConstantValue_2()`                | 11930        | Obfuscation | Returns `0xAB`. |

## Notes

* The "i440FX/PIIX3 init" routines belong to the upstream research
  fork's emulator scaffolding, not to the P2K game itself; Encore
  does not use this scaffolding because the PRISM option ROM rejects
  i440FX/PIIX3 chipsets as non-MediaGX.
* The XOR `0x3A` decode and the `^0xAB - 0x4D` string decoder are
  the two pieces of light obfuscation Williams applied to the
  shipped binaries; both are trivial and well-documented in the
  reverse-engineering notes.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
