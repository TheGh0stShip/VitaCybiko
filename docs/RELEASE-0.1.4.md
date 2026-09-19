# VitaCybiko v0.1.4-preview

Physical-Vita audio pacing candidate.

- The Vita frontend now prefers a 48 kHz signed 16-bit stereo SDL audio device
  and converts the emulator's internal 8-bit mono speaker samples before
  queueing. If that device cannot be opened, it falls back to the previous
  8-bit mono path.
- SDL queued audio now keeps a small one-frame prebuffer instead of running
  nearly empty. This should reduce underrun crackle/choppiness on physical Vita
  while still bounding backlog to four emulated frames so audio does not drift
  far behind gameplay.
- The frontend audio regression now checks the prebuffered queue and stale
  backlog discard behavior.
- Package metadata is bumped to `01.04`.

The VPK and extracted app folder were staged over FTP to a physical Vita for
user testing. Audible quality still needs physical-device confirmation; this is
not a claim of perfect audio timing.
