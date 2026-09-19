# Windows Vita3K setup

Install the VPK through Vita3K's File menu. Locate the configured Vita storage
directory (the **pref-path** setting), then use its `ux0/data/VitaCybiko/` tree.
Do not assume that storage is beside Vita3K.exe: Windows normally stores it in
the user's AppData directory.

Follow [model firmware mapping](MODELS.md). Existing saves must be preserved
when replacing the application. Installing a new VPK does not require deleting
`ux0/data/VitaCybiko/`.

The release was tested with Windows Vita3K 0.2.1, build 4095-84184a36, OpenGL,
960×544. The screenshots are direct captures of that application's client
window, not generated screen mockups.

Vita3K maps PC keys to Vita controls. Check its controller settings; a PC letter
may operate a Vita button rather than type that letter into Cybiko. The on-screen
touch keyboard provides deterministic text input using mouse clicks. Start +
Select returns to the model menu after saving. Close Vita3K cleanly afterward.

A missing-firmware error is intentional. Classic V1, V2 and Xtreme ROMs are not
interchangeable. No ROM download service is included.
