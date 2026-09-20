# Compatibility

## Verified and unverified

| Model / feature | Evidence |
| --- | --- |
| Classic V2 / CyOS 1.3.58 | Genuine first-run setup, Main Desktop, Pinball gameplay/exit, Calculator 2 + 3 = 5, Text Editor save/reopen in Windows Vita3K |
| Classic V1 / original stock | Matching firmware staged; host smoke reaches desktop; Windows Vita3K reaches stock desktop, but app-by-app behavior and performance are not validated |
| Xtreme / CyOS 1.5.08 | Matching firmware staged; Windows Vita3K ran an application and produced 01.14 timing logs, but only ~9.9 guest frames/sec; app-by-app behavior and performance remain open |
| Physical Vita / PSTV | v0.1.3 fixed VPK and app folder staged over FTP to a physical Vita; launch/performance evidence still pending user confirmation |
| Persistence | Setup, a created text document and clock continuity survive Vita3K restart; RAM/flash binding and corruption rejection tested on host |
| Input | Touch setup and game launch observed; host tests cover controller/touch isolation, modifiers, focus release and layout switching |
| Audio | Speaker core, suspend/resume tests and v0.1.4 low-latency/prebuffer frontend queue bounds are covered; audible fidelity remains user-test territory |
| Radio / accessories | No end-to-end wireless, CyWIG, PC sync, USB or MP3 support |

## Classic bundle

The tested serial-flash image is the C4PC Classic V2 image, SHA-1
`e414d6d2f876c7c811946bcdfcb6212999412381`. Its contents include the desktop,
chat, email, people, phonebook, settings, calculator, notes, organizer, file
manager, Labyrinth, Pinball Pro, Blazing Boards, Cybiko Superbike, Men's Room 2,
Reversi 3 and Study Stools, along with libraries and system data.

Presence in a filesystem is **not** a runtime compatibility result.
The image is a later Classic software set, not proven to be the exact original
day-one retail bundle. CyLandia_Info is present; that does not establish that
the full CyLandia application is installed. The original launch set remains
unverified.

Classic applications are loaded from serial flash. Dropping files into a
Classic `apps/` folder does not install them: Classic uses a different filesystem
format from the Xtreme importer.

## Xtreme software tools

The latest physical-Vita 01.14 log contains 77 one-second rows for the
Xtreme profile. It records 5.88 guest frames/sec across the captured run, with
zero SDL audio underrun counters after the continuity change. This does not
mean audio is perceptually correct: repeated last-frame audio is a bounded
gap-avoidance measure while the guest falls behind. The Xtreme CPU path remains
the highest-priority performance backlog item.

The same current capture contains 92 Classic V1 rows and 127 Classic V2 rows.
Their aggregate guest rates are 42.00 and 18.06 frames/sec, respectively,
versus 5.88 for Xtreme. This confirms the severe slowdown is workload/model
dependent rather than a universal audio-device failure; all three logs report
zero producer-observed audio underruns after the continuity work. Classic V2's
newly observed 18 FPS result also shows that the interpreter/presentation
budget can regress outside Xtreme and must be measured per workload.

`tools/extract_cd_pack.py` recovers a user-supplied original CD CAP pack without
running its Windows installer. The inspected pack contains 13 applications and
two libraries (1,606 of 2,000 CFS blocks). They are **Xtreme CD software**, not a
replacement for the Classic launch bundle. Source software is not redistributed.

`tools/prepare_apps.py` validates and stages selected Xtreme applications.
Up to 64 .app/.dl files in `xtreme/apps/` and its `Libraries/` subfolder are
imported alphabetically. Existing user data also consumes capacity. Invalid
input or an invalid save stops loading without silently replacing the save.

## Emulation limits

CPU timing is approximate; DAA/DAS and some system-control modes are incomplete.
DTC support covers the observed serial-flash transfer path, not every DMA mode.
SCI transmit completion and RTC I2C/calendar handling were corrected to allow
Classic boot; no firmware code, task flags or return addresses are patched.
RTC alarm/event-counter modes and complete serial/radio behavior are not claimed.
The preview's clock-reset fault was caused by discarding Classic battery-backed
SRAM. v0.1.1-preview restores RAM as well as RTC and flash, and clock continuity
was verified across a Vita3K restart. RTC alarm modes remain unverified.

Classic saves are size-checked, not fully CFS-integrity-validated. Guest firmware
can still write incorrect data if it encounters an emulation bug. Keep backups.
Short Classic taps previously missed the guest scan window. v0.1.1-preview holds
them for eight emulated frames; a 40 ms Vita3K touch now opens Pinball's quit
dialog. Confirm Quit to return to Games. Calculator intentionally uses one Esc
to clear and two to exit. Start + Select remains the save/model-menu shortcut.
v0.1.2-preview reduces queued-audio latency and allows limited emulation
catch-up when rendering falls behind; it does not make the core cycle-accurate
or guarantee full-speed Xtreme playback.
See [verification](VERIFICATION.md) for the tested artifact and actual captures.
