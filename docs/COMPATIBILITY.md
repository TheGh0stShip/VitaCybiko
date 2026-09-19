# Compatibility

## Verified and unverified

| Model / feature | Evidence |
| --- | --- |
| Classic V2 / CyOS 1.3.58 | Genuine first-run setup, Main Desktop, Pinball gameplay/exit, Calculator 2 + 3 = 5, Text Editor save/reopen in Windows Vita3K |
| Classic V1 / original stock | Profile, memory map, keyboard and DataFlash tests; matching firmware unavailable, boot unverified |
| Xtreme / CyOS 1.5.08 | Profile and host core/CFS/import tests; matching firmware unavailable, boot unverified |
| Physical Vita / PSTV | Cross-built VPK only; no physical-device run |
| Persistence | Setup, a created text document and clock continuity survive Vita3K restart; RAM/flash binding and corruption rejection tested on host |
| Input | Touch setup and game launch observed; host tests cover controller/touch isolation, modifiers, focus release and layout switching |
| Audio | Speaker core and frontend suspend/resume tests; audible fidelity not verified |
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
See [verification](VERIFICATION.md) for the tested artifact and actual captures.
