# Classic device icon

`icon-classic-master.png` is new artwork generated with the built-in image
generation tool on 2026-09-19. `../sce_sys/icon0.png` is the 128×128 RGBA LiveArea
icon, downsampled with ImageMagick's Lanczos filter. The master is not bundled
in the VPK. The hardware artwork does not imply Classic emulation is complete.

Shape reference: [Science Museum Group's Cybiko Classic](https://collection.sciencemuseumgroup.org.uk/objects/co8366254/cybiko-classic-hand-held-computer-with-batteries).
The museum photograph was used as a visual reference, not packaged in the app.
This is an illustration, not a hardware schematic; tiny key legends are not
an authoritative keyboard mapping.

## Final generation prompt

Use case: product-mockup. Asset type: square PlayStation Vita LiveArea application icon, must read clearly when reduced to 128x128 pixels. Primary request: a literal original Cybiko Classic handheld device, faithful to the physical device in the reference photograph. Image 1 is a HARDWARE SHAPE REFERENCE, not a photograph to reuse. Generate a new polished studio product illustration of this same original Classic: translucent blue-violet rippled/scalloped shell, wide monochrome olive LCD in upper half, seven little white round function buttons above the display, the distinctive broad white directional control below the LCD at left, oval white Del/Ins/Tab/Select/Enter keys at right, and dense white oval number-row plus QWERTY keyboard underneath. Keep the original curved/wavy Classic silhouette and real proportions. Show one entire device, nearly face-on in a very mild three-quarter view, centered with a subtle diagonal tilt, filling about 88% of the square canvas. Deep dark navy background, soft cyan rim lighting and restrained soft shadow to separate the plastic body; large readable shape, crisp white keys, clean olive-green display with very simple monochrome desktop glyphs. No extra objects, no hands, no caption or VitaCybiko lettering outside device, no circular border. Preserve realistic hardware rather than turning it into a generic phone or game controller. Small device logo may read 'Cybiko'.

## Rebuild the packaged icon

```bash
convert assets/icon-classic-master.png -filter Lanczos -resize 128x128 -strip PNG32:sce_sys/icon0.png
```
