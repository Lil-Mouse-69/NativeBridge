# NativeBridge

**Experimental · v0.4.1 · Apple silicon**

See [validation and known limits](VALIDATION.md). No game files, keys or firmware are included.

A native Apple-silicon Mac app for building your own Switch game dumps into Mac apps and iPhone/iPad IPAs using existing community backends.

## Start

Open **NativeBridge.app** (or unzip **NativeBridge.zip**). Select a directory containing **one game's** base NSP/XCI, update and DLC, select your `prod.keys`, and choose an output folder. On first launch, review **Setup & checks** and approve missing-tool downloads. Use **Check files & space**, then **Build game** to review and approve the build.

The setup screen guides Xcode and Homebrew installation with separate approvals. Tool setup reuses installed tools and downloads missing components only after approval; nxnative also needs the .NET SDK, and Mush8 uses Rust. Most current game backends need macOS/iOS 26 and Apple silicon even though the wrapper itself targets macOS 14. Use only your own dumps, keys and firmware. No game content or keys are included.

## Backends

- **MacNativeGames**: automatic for Tears of the Kingdom and Breath of the Wild. Uses `build_app.py` and the project's device installer. Supply unpacked firmware for NSP builds. Select the base/update explicitly under Advanced if filenames are ambiguous. DLC is supported only where the upstream game/version supports it (currently BOTW's supported separate-update workflows).
- **Mush8**: automatic for Mario Kart 8 Deluxe. Requires base NSP, update NSP 3.0.5 or 4.0.1, firmware and optionally one Booster Course Pass DLC. The Mac output is the Mush8 launcher: open it, choose the original game folder, and press Prepare. If your key file is elsewhere, place it in that game folder as `prod.keys`; firmware should be `Firmware` and DLC `dlc.nsp`. The iPhone workflow builds the IPA, signs/installs the app, then transfers keys, game files and firmware to its Documents directory. An IPA exported without installation still requires game-data import inside Mush8.
- **nxnative**: default for other games; also selectable for Zelda or Mario Kart, including Mario Kart XCI. Processes the folder's package metadata, selects the newest update and includes DLC. Support and quality vary by game/version; an unknown title is not a promise of compatibility. Community MacNativeGames ports beyond Zelda use this route, rather than their individual scripts.

Automatic routing is a filename/title-ID hint, not decrypted content identification. Files are found recursively; build/cache folders are skipped. Keep only one game's packages in the selected folder. For specialized backends, label update and DLC filenames accordingly. All actual content validation belongs to the upstream build tools.

## iPhone installation

Choose iPhone/iPad and enable **Sign and install**. Connect and unlock the device, enable Developer Mode, trust the Mac, and sign into Xcode Settings → Accounts. Click **Refresh accounts & devices**. A single signing team/device is selected automatically; choose from the selectors if several are found. No Team ID needs to be typed.

Zelda uses its upstream signing installer. Mush8 uses its upstream Xcode installation script, followed by `devicectl` file transfer. nxnative builds the unsigned IPA, prepares the game's Xcode project, builds with automatic signing, and installs the signed app using `devicectl`. The exported IPA remains unsigned; the device app is signed separately. Apple provisioning, entitlements, account limits and available device memory can prevent installation or play. Failure logs remain visible, and an already-created IPA remains available even if installation fails. Free-account mode is a Mush8 option only; nxnative retains its upstream entitlements, which your team must support.

## Progress and local storage

Build output is streamed live. The first estimate is a rough 45 minutes and can be exceeded substantially; later estimates reuse the duration of the previous successful build for that folder/backend/platform. The activity bar is indeterminate: it does not claim measured percentage completion. Cancellation stops the command's process group and retains intermediate work.

Repositories, job caches and build duration history are stored under `~/Library/Application Support/NativeBridge`. A backend is cloned on demand and its exact commit is printed in the log. Existing checkouts are reused without automatic updates. To refresh a backend, quit the app and remove only its folder under `backends`; the next build clones it again. Sources are executed locally and may download their own dependencies. No upload feature is implemented by NativeBridge; upstream tools retain their own behavior. Key contents are not stored in app preferences, only the chosen path. Mush8's local backend checkout holds symlinks to selected files. Device transfer sends your files to the selected iPhone. Saved logs may contain filenames and local paths.

Use **Save log** before a new operation if you need a diagnostic record. The displayed log retains its latest approximately 140–180 thousand characters. Do not delete caches while a build is running. Output artifacts use timestamped names; original game packages are not modified by this wrapper.

## Build and validation

Source is included in `Source/`. Rebuild with `bash build.sh` using Xcode's Swift compiler. Run orchestration tests with:

```sh
/usr/bin/python3 -m unittest discover -s Source -p 'test_*.py' -v
```

See [VALIDATION.md](VALIDATION.md) for current checks and outstanding device tests. Earlier version sections below describe historical validation.

## Upstream references

- https://github.com/ly05010419/MacNativeGames
- https://github.com/c22dev/mush8
- https://github.com/seedds/nxnative

Build interfaces checked on October 26, 2026. Upstream changes can require adapter updates. This app contains no upstream source distribution; each backend is downloaded separately and retains its own license.

## Version 0.4.0: requirements and iPhone settings

Setup checks only tools needed for the selected game and platform. BOTW/TOTK folder names route correctly, installed .NET 10 SDKs outside Homebrew are detected, and .NET is required only for nxnative. Cached shared fonts and existing build-cache storage are reused.

The iPhone gear sits at bottom center and follows the touch controls when controller use conceals them. BOTW 1.6.0/1.9.0 gains 540p and native 720p settings; restart the game after changing resolution. Rebuild and reinstall existing games to receive these changes. Controller hide/show still needs a physical-controller test.

## Version 0.2: guided setup and download approval

The first launch opens **Setup & checks** automatically. It performs local checks only. Checks cover the currently selected game/backend and target platform. The checklist checks Apple silicon, the macOS version, full Xcode and its first-launch setup, Homebrew, backend tools, a working Metal compiler, and the iOS SDK when applicable. Installed command-line tools are reused where detected, including tools outside Homebrew. Python/Xcode bootstrap detection runs natively so a machine without developer tools is not sent into a Python installation prompt just to show onboarding.

**Install missing tools…** shows the exact missing-tool list and an estimated installation footprint before you approve downloads. The backend rechecks the list before installation: newly missing tools require another approval. Homebrew installation has its own confirmation and runs its official installer in Terminal, where you can review its changes and enter your Mac administrator password. Xcode installation and license acceptance are completed in Apple's interfaces. The .NET package is fetched and checksum-verified by Homebrew, then opened in Apple's Installer for administrator authentication; return and click **Check again** after it completes. No dependencies were installed while developing/testing this update.

**Check files & space** checks NSP/XCI container headers, key-file syntax without displaying key values, package-selection ambiguity, and firmware NCA presence where required. These are preliminary checks, not full cryptographic validation or a guarantee that the firmware, keys or game version are correct. Actual title IDs, content integrity and compatibility are still validated by the upstream backend. All checks repeat before a build; missing tools or invalid inputs block it.

Disk estimates combine cache, output, and an extra signing copy when paths share a volume, with a 10 GiB reserve. The setup estimate accounts for tool-installation locations rather than assuming the game cache drive holds the tools. Change **Advanced → Build cache location** and the output folder to use another disk. Long-running build/setup commands check available space every two seconds and stop their process group if it falls below 5 GiB. Estimates are conservative heuristics, not exact download sizes or guaranteed peak usage. The separately opened Homebrew/.NET installers are outside NativeBridge's process monitor; their own installers handle final installation checks. Disk checks cover the Mac, not remaining storage on the iPhone.

A build has a separate approval for its backend and transitive dependency downloads, even when setup tools are already installed. Checking alone never grants download permission.

### Apple account and iPhone

Team-ID typing has been replaced by **Sign in through Xcode…** and **Refresh accounts & devices**. Sign in once in Xcode Settings → Accounts with your Apple Account, then return. NativeBridge reads local Xcode team metadata/development certificates and paired iOS devices. A single result is selected automatically; otherwise select the appropriate team and iPhone. If a signed-in team isn't detected, use Xcode's Manage Certificates to create an Apple Development certificate, then refresh. These checks do not read passwords or private keys.

This implements Xcode-based signing and device installation, **not an embedded AltStore Apple-ID login or AltStore background refresh service**. Apple account, device-trust, provisioning, memory and seven-day free-signing restrictions remain. The IPA export stays unsigned while the device app is signed separately. The earlier instructions to manually enter a Team ID are superseded by these selectors.

Validation for this update: 24 automated tests covering consent enforcement, changed download plans, reuse of installed tools, low-space blocking, shared-volume accounting, stopping a quiet process when space runs low, input validation, and account metadata parsing; Swift compilation and signature verification; first-run checklist and download-cancellation UI checks; live account/device discovery. End-to-end downloads, game conversion, Apple provisioning and phone installation have not been exercised.

## Version 0.3: settings inside generated iPhone apps

Rebuild and reinstall each game using NativeBridge 0.3 to include a **gear button** in the upper-right corner of the iPhone app. Existing installed apps and previously exported IPAs are not changed automatically.

The menu includes:

- **Controls opacity:** 0–100% of the original control layer's opacity, saved per app and applied immediately. At 0%, the controls are invisible but touch input remains active. The gear stays visible unless controller use hides the touch controls.
- **Keep screen awake:** saved per app.
- **Rendering resolution:** supported options for the selected backend/game, saved for the next launch.
- **Reset NativeBridge settings:** restores these preferences without deleting saves or changing the original launcher preferences.

Opening the menu does **not pause gameplay**. Close the app from the iPhone app switcher and reopen it after changing graphics options. Original launcher options such as rumble, frame-rate displays and game-specific settings remain available where the backend provides them.

| Backend | Rendering options in the new menu |
| --- | --- |
| MacNativeGames, Tears of the Kingdom 1.4.3 | Internal 960×540 or 1280×720; output is still upscaled. The existing render-size hook is included during export. |
| MacNativeGames, Breath of the Wild 1.6.0 / 1.9.0 | Internal 960×540 or native 1280×720; restart after changing. |
| MacNativeGames, other Zelda versions | Resolution is unavailable unless supported by the backend hook. |
| nxnative | 50%, 75%, or 100% of the base resolution only when the bundled game profile exposes `DM_RENDER_SCALE`. 75% of a 720p base is 540p; this does not mean every game has a 720p base. Explicit `env.txt` settings retain priority. |
| Mush8 / Mario Kart 8 Deluxe | Handheld 720p or docked 1080p. This backend does not expose a verified 540p option. |

The build applies reversible source patches to its cached backend before compiling the iPhone app. Original files are backed up under `.nativebridge-originals`, and `.nativebridge-settings.json` records their hashes. Reapplying the same patch is idempotent. If expected integration points change or patched sources have been edited, the build stops rather than overwriting those changes or silently omitting the menu. Refreshing a backend requires another build-download approval. Mac game builds do not gain this iPhone menu.

Validation: 30 automated tests passed, including integration against fetched source snapshots for all three backends, patch idempotence, original-file backups, and refusing changed integration points. The UIKit settings code compiled and linked against the installed iPhoneOS SDK for all three backends and the unsupported BOTW configuration. The Mac wrapper compiled and passed signature verification. **The settings menu has not been run in a real game or on an iPhone.** No simulator runtime was installed, so visual and touch behavior still need device testing. No game files, keys, firmware or additional dependencies were downloaded for this feature.

### Version 0.3.1: 30 / 60 FPS target

The in-game gear menu now includes **Frame-rate target → Default / 30 FPS / 60 FPS** for MacNativeGames' Zelda iPhone builds and nxnative games whose bundled profile exposes both frame-rate targets. Close and reopen the game after changing it. A 60 FPS target does not guarantee the device can sustain 60 FPS. Explicit nxnative `env.txt` settings retain priority. Mush8 has no verified 30/60 pacing switch, so the menu marks it unavailable rather than changing game speed incorrectly.

Settings updates can now upgrade a previous NativeBridge patch in place using verified original-source backups. Modified source files or backups still block the upgrade. Rebuild and reinstall the game to get this change. Compilation/link checks and 31 automated tests passed; game/device testing remains outstanding.
