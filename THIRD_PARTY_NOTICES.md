# Third-party notices

- The portable core in `third_party/cybiko-c-emulator` derives from
  [Dan Berkowitz's C emulator](https://github.com/daberkow/cybiko-c-emulator).
  Its MIT license and copyright notice are retained in that directory.
- Classic hardware profiles and serial DataFlash behavior also reference
  [Dan Berkowitz's MIT-licensed Java emulator](https://github.com/daberkow/cybiko-java-emulator).
  VitaCybiko does not include its firmware-specific task-state bypasses.
- Host tests use the vendored Acutest header, whose license is in the header.
- Vita builds link [SDL2](https://wiki.libsdl.org/SDL2/FAQLicensing) (zlib license)
  and [SDL2_gfx](https://github.com/ferzkopp/SDL2_gfx) (zlib license), supplied by
  VitaSDK. VitaSDK tools and libraries retain their respective licenses.
- H8S hardware behavior was checked against Renesas documentation and the
  MAME device implementations. MAME is a reference, not a linked dependency.
  See the upstream core's acknowledgments for its porting history.
- The device icon is original generated artwork; see [asset provenance](assets/README.md).
  Cybiko names, firmware, applications and their on-screen artwork belong to
  their respective owners. Runtime screenshots document emulator testing;
  they do not license the depicted software for redistribution.

No firmware, commercial application packs, personal saves or original CD media
are included in the source repository or VPK. Supply legally obtained images.
This is an independent homebrew project, not an official Cybiko or Sony product.
