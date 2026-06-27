# The Central Scrutinizer

Central Scrutinizer is a device-hosted web manager for Leaf handhelds. This fork currently targets the Miniloong Pocket 1 (`mlp1`) on stock firmware through Leaf, while keeping the app/platform boundary ready for future Leaf ports.

Launch it on the handheld, open the URL shown on screen from another browser on the same local network, and manage library files from the browser.

## Features

- Library grouped by platform — one card per console (folder aliases and
  emulator variants fold into the base system from the `systems.json` catalog
  patterns, so uploads target the canonical public folder), with
  ROM/save-state/save/BIOS/cheat counts
- ROM, save, BIOS, cheat, and full SD file browsing
- Upload, folder upload, ZIP extraction, download, rename, delete, and folder creation
- ROM artwork replacement through Leaf `Images/<SYSTEM>/<ROM>.png`
- RetroArch save-state discovery from Leaf `States/`
- Mac dot-cleanup for `.DS_Store`, `._*`, `__MACOSX`, and top-level macOS transfer artifacts
- Leaf app log browsing, live tail, and download
- Optional browser terminal access
- Foreground handheld pairing screen plus background server handoff after a browser is trusted

Playlist, capture, and overlay management were removed during the Leaf fork cleanup. They should return only as Leaf-native tools with public Jawaka/Leaf path contracts.

## Build

Central Scrutinizer depends on the sibling Catastrophe checkout from the UMRK workspace:

```sh
make mac
make package-platform PLATFORM=mlp1
```

The MLP1 build uses the workspace toolchain image:

```sh
MLP1_TOOLCHAIN_IMAGE=ghcr.io/utility-muffin-research-kitchen/mlp1-toolchain:local make package-platform PLATFORM=mlp1
```

The packaged app is written to:

```text
build/mlp1/package/CentralScrutinizer.pak
```

## Leaf Install

Leaf owns staging. From the sibling `Leaf/` repo:

```sh
make stage-app APP=CentralScrutinizer DEVICE=mlp1
```

The app is installed as:

```text
Apps/mlp1/CentralScrutinizer.pak/
```

`launch.sh` sources Leaf's platform environment when present and falls back to the MLP1 defaults:

- `SDCARD_PATH=/mnt/sdcard`
- `UMRK_PLATFORM_PATH=$SDCARD_PATH/.system/leaf/platforms/mlp1`
- `SYSTEM_PATH=$UMRK_PLATFORM_PATH` compatibility alias
- `USERDATA_PATH=$SDCARD_PATH/.userdata/mlp1` (durable user/app data at the SD root, not under the release-managed `.system` tree)
- `SHARED_USERDATA_PATH=$SDCARD_PATH/.userdata/shared`
- `CORES_PATH=$UMRK_PLATFORM_PATH/cores`
- `INFO_PATH=$UMRK_PLATFORM_PATH/info`
- `LOGS_PATH=$USERDATA_PATH/logs`

## Browser Pairing

1. Launch Central Scrutinizer on the handheld.
2. Open the shown URL from a phone, tablet, or computer on the same network.
3. Enter the PIN shown on the handheld, or press `Y` to show a QR pairing code.
4. Use handheld settings to enable terminal access, revoke trusted browsers, or hand the server to background mode.

Terminal access is disabled by default on handheld builds.

## Managed Locations

The Leaf fork uses these SD card locations:

- `Roms/`
- `Images/`
- `Saves/`
- `States/`
- `BIOS/`
- `Cheats/`
- `.system/leaf/platforms/mlp1/cores/`
- `.system/leaf/platforms/mlp1/info/`
- `.userdata/mlp1/CentralScrutinizer/`
- `.userdata/mlp1/logs/`

## Tests

```sh
make test-native-all
make web-test
make test-smoke
```

## Credits

Platform icons used by the dashboard are derived from the libretro Systematic theme in RetroArch assets:
https://git.libretro.com/libretro-assets/retroarch-assets/-/tree/e11d6708b49a893f392b238effc713c6c7cfadef/xmb/systematic

The original app was inspired by earlier handheld web dashboards and `kitchen`; this fork is now Leaf-only.
