# Third-party notices and attribution

dArkOS-S40-installer adds the S40 launcher on top of **dArkOS** (https://github.com/christianhaitian/dArkOS, by christianhaitian and
contributors, MIT for its scripts). The installer does **not** contain dArkOS: you need a working dArkOS card. It only installs files
under `/opt/s40`, `/opt/s40web`, `/opt/ytdl`, `/opt/j2me` and two systemd services. Each component keeps its own license; if you are an
author and want a credit corrected or a file removed, please open an issue.

## Written for this project (MIT, see LICENSE)

`src/s40launcher` (launcher, YouTube, weather, radio, TV, file manager, `ytpad` gamepad bridge), `src/s40web` (browser front-end),
`src/common`, `src/nokia_s40_ui` (icon set), `install.sh`, `uninstall.sh`, `payload/` scripts. The look is only *styled after* Nokia
Series 40 phones; no Nokia code, fonts, sounds or artwork are used.

## Installed from Debian / dArkOS repositories (apt)

mpv and FFmpeg (GPL/LGPL), WPE WebKit and wpebackend-fdo (LGPL/BSD), GStreamer (LGPL), libvterm (MIT), fonts-wqy-microhei (GPL-2 with font
exception / Apache), OpenJDK 21 (GPL-2 + Classpath Exception), Mesa/GLVND/libwayland-egl (MIT and others), espeak-ng-data.
The low-battery voice clips (`payload/opt/s40/assets/sounds`) were generated with espeak-ng (GPL-3.0; generated audio is not restricted).

## Downloaded by the installer from official sources (not redistributed here)

| Component | License | Source |
|---|---|---|
| yt-dlp | Unlicense | https://github.com/yt-dlp/yt-dlp |
| Node.js 22 | MIT | https://nodejs.org |
| 7-Zip (`7zzs`) | LGPL-2.1 with the unRAR restriction | https://www.7-zip.org |

## Included in the prebuilt binaries archive (`s40-bin-aarch64.tgz`)

| Component | License | Source |
|---|---|---|
| FreeJ2ME-Plus (`freej2me-plus-sdl.jar`, `sdl_interface`) | GPL-3.0 (`LICENSE-FreeJ2ME-Plus.txt` is in the archive) | https://github.com/TASEmulators/freej2me-plus |
| J2ME Loader port "jlr" (`jlr.jar`, `libjlr.so`) | Apache-2.0 (`LICENSE`, `NOTICE` in the archive) | https://github.com/nikita36078/J2ME-Loader |
| Ad-block filter lists (EasyList, uBlock filters, ABPVN, Peter Lowe, ...) | CC BY-SA 3.0 / GPL-3 / per list | https://easylist.to , https://github.com/uBlockOrigin/uAssets , https://github.com/abpvn/abpvn |
| Fonts DejaVu Sans, Noto Sans | Bitstream Vera license (`docs/licenses/DejaVu-LICENSE.txt`) / SIL OFL 1.1 (`docs/licenses/OFL-1.1.txt`) | https://dejavu-fonts.github.io , https://fonts.google.com/noto |
| `bell.wav` (alarm sound) | from dArkOS (`scripts/round_end.wav`), MIT | https://github.com/christianhaitian/dArkOS |

## Data used at run time

| Data | Terms | Source |
|---|---|---|
| TV channel lists (`payload/opt/s40/assets/tv`) | Unlicense/CC0; links only, no video is stored | https://github.com/iptv-org/iptv |
| Radio stations by country (`assets/radio/dai.m3u`) | open community database | https://www.radio-browser.info |
| Weather forecast | CC BY 4.0, free for non-commercial use | https://open-meteo.com |
| Cover art | downloaded by the device from the libretro thumbnail servers; not stored here | https://thumbnails.libretro.com |

## What is not included

No game ROMs, BIOS files, commercial games, APKs or Java games. YouTube, Facebook and Telegram are only opened through their public
web sites (YouTube through yt-dlp); their names and logos belong to their owners and this project is not affiliated with them. The TV
lists are only links to publicly available streams; the content belongs to the broadcasters. Downloading YouTube content may be
against YouTube's terms in your country; use at your own responsibility. This project is free and non-commercial: do not sell devices
or cards with it pre-installed.
