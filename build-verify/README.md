# Native TFT Build Verification

Use [`build-verify/native-tft-docker.sh`](/Users/svk/Documents/Projects.nosync/device-ui/build-verify/native-tft-docker.sh) for reproducible verification of the `native-tft` target.

## Preconditions

- `docker` is installed and running
- sibling checkout exists at [`../meshtastic-firmware`](/Users/svk/Documents/Projects.nosync/meshtastic-firmware)
- for UI launch, XQuartz is installed on macOS

## Commands

Build only:

```bash
./build-verify/native-tft-docker.sh build
```

Build and launch `meshtasticd` with X11 forwarding:

```bash
./build-verify/native-tft-docker.sh run
```

The `run` mode prepares a temporary Portduino filesystem and copies [`source/apps/scripts`](/Users/svk/Documents/Projects.nosync/device-ui/source/apps/scripts) into `/apps/scripts`, so Berry demo apps can load at runtime.

Open an interactive shell in the prepared image:

```bash
./build-verify/native-tft-docker.sh shell
```

## Why this script exists

`device-ui` is built as a library from the `meshtastic-firmware` PlatformIO target `native-tft`, not via standalone `cmake`. The script mounts both repositories into the Docker image from [`/Users/svk/Documents/Projects.nosync/meshtastic-firmware/Dockerfile.native`](/Users/svk/Documents/Projects.nosync/meshtastic-firmware/Dockerfile.native) and runs the canonical firmware build command:

```bash
pio run -e native-tft
```

## Agent Workflow

1. From repo root, run `./build-verify/native-tft-docker.sh build`.
2. If the user asks for compile verification only, stop there and report the result.
3. If the user asks for a UI smoke test, run `./build-verify/native-tft-docker.sh run`.
4. The `run` path also stages Berry demo scripts into the Portduino filesystem.
5. Do not use ad-hoc standalone `cmake` to verify `native-tft`; that checks the wrong build path.
