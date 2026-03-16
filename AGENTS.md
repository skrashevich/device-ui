# AGENTS.md

## Native TFT Verification

- To verify changes in this repo, prefer the canonical Docker workflow instead of standalone `cmake`.
- Run build verification from the repo root with:
  - `./build-verify/native-tft-docker.sh build`
- For a UI/X11 smoke test on macOS with XQuartz:
  - `./build-verify/native-tft-docker.sh run`
- `run` also stages [`source/apps/scripts`](/Users/svk/Documents/Projects.nosync/device-ui/source/apps/scripts) into the Portduino filesystem so Berry demo apps can load.
- This repo is consumed by the sibling firmware checkout at [`../meshtastic-firmware`](/Users/svk/Documents/Projects.nosync/meshtastic-firmware), where PlatformIO env `native-tft` links `../device-ui`.
- If the firmware checkout is not in the default sibling path, set `MESHTASTIC_FIRMWARE_DIR=/abs/path/to/meshtastic-firmware`.
- Use `build` for normal compile verification. Use `run` only when the task needs an actual native UI launch.
