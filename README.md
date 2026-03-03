![Ship of Harkinian](docs/shiptitle.darkmode.png#gh-dark-mode-only)
![Ship of Harkinian](docs/shiptitle.lightmode.png#gh-light-mode-only)

This is a fork of jeromkiller/Shipwright_archipellago that adds an auto Map Tracker

It is a version of Ocarina of Time Ship of Harkanian with an auto MapTracker that aim to welcome new players to be able to enjoy the archipelago randomiser.

## Map Tracker
This map Tracker replaces the Check Tracker window (Still accessible) and add all of the maps with every check on it. It updated in real time when you change zone, when you do the checks or when the time of day or age of Link changes.

<img width="1461" height="950" alt="hyrule" src="https://github.com/user-attachments/assets/64a023f3-4485-47fd-9dc2-63d17811ded0" />
<img width="1466" height="956" alt="Capture d&#39;écran 2026-03-03 125456" src="https://github.com/user-attachments/assets/df5fc914-3f80-4849-8218-b4224918a3ff" />
<img width="1255" height="1207" alt="Capture d&#39;écran 2026-03-03 125531" src="https://github.com/user-attachments/assets/5819486f-99b9-413b-a011-b7a1253cda87" />
<img width="1256" height="1152" alt="Capture d&#39;écran 2026-03-03 125707" src="https://github.com/user-attachments/assets/2c4bcf28-9ae2-409a-be68-d9f42574636e" />

## Time and Age requirements
When a check is Yellow it means you can do the check but you need to change the time or the age. Hover on the check to see the requirements

<img width="445" height="157" alt="Capture d’écran 2026-03-03 132536" src="https://github.com/user-attachments/assets/a00b5ed8-733d-4caf-84c0-665e4d061ca1" />

## Hints
There is also a hint system that adds hints on some of the checks. (witch can be edited in the tool editor mentionnel just below) There is not many hints at the moment but it will improve over time.

<img width="372" height="152" alt="Capture d’écran 2026-03-03 132951" src="https://github.com/user-attachments/assets/e4acf165-7853-43c3-b561-063216d286e8" />
<img width="414" height="159" alt="Capture d’écran 2026-03-03 133019" src="https://github.com/user-attachments/assets/09f3fa5d-cfae-47bf-abb7-ec30209b6d97" />

## Ressources
The Map tracker works with a ressource pack that can be edited using this python tool : https://github.com/Symon799/SoH_Map_Ressource_Maker
The Ressource pack in the mods/check_tracker_map_pack folder
If a check is missing, or not at the right place or you want to add a hint you can edit the ressource pack using this tool

Big thank you to Titrok with whom I made this auto Map Tracker
Also thank you to Peardian for the original images of the maps (https://www.vgmaps.com/Atlas/N64/index.htm)

----

## Website

Official Website: https://www.shipofharkinian.com/

## Discord

Official Discord: https://discord.com/invite/shipofharkinian

If you're having any trouble after reading through this `README`, feel free to ask for help in the Support text channels. Please keep in mind that we do not condone piracy.

# Quick Start

The Ship does not include any copyrighted assets.  You are required to provide a supported copy of the game.

### 1. Verify your ROM dump
You can verify you have dumped a supported copy of the game by using the compatibility checker at https://ship.equipment/. If you'd prefer to manually validate your ROM dump, you can cross-reference its `sha1` hash with the hashes [here](docs/supportedHashes.json).

### 2. Download The Ship of Harkinian from [Releases](https://github.com/HarbourMasters/Shipwright/releases)

### 3. Launch the Game!
#### Windows
* Extract the zip
* Launch `soh.exe`

#### Linux
* Place your supported copy of the game in the same folder as the appimage.
* Execute `soh.appimage`.  You may have to `chmod +x` the appimage via terminal.

#### macOS
* Run `soh.app`. When prompted, select your supported copy of the game.
* You should see a notification saying `Processing OTR`, then, once the process is complete, you should get a notification saying `OTR Successfully Generated`, then the game should start.

#### Nintendo Switch
* Run one of the PC releases to generate an `oot.o2r` and/or `oot-mq.o2r` file. After launching the game on PC, you will be able to find these files in the same directory as `soh.exe` or `soh.appimage`. On macOS, these files can be found in `/Users/<username>/Library/Application Support/com.shipofharkinian.soh/`
* Copy the files to your sd card
```
sdcard
└── switch
    └── soh
        ├── oot-mq.o2r
        ├── oot.o2r
        ├── soh.nro
        └── soh.o2r
```
* Launch via Atmosphere's `Game+R` launcher method.

### 4. Play!

Congratulations, you are now sailing with the Ship of Harkinian! Have fun!

# Configuration

### Default keyboard configuration
| N64 | A | B | Z | Start | Analog stick | C buttons | D-Pad |
| - | - | - | - | - | - | - | - |
| Keyboard | X | C | Z | Space | WASD | Arrow keys | TFGH |

### Other shortcuts
| Keys | Action |
| - | - |
| ESC | Toggle menu |
| F2 | Toggle capture mouse input |
| F5 | Save state |
| F6 | Change state |
| F7 | Load state |
| F9 | Toggle Text-to-Speech (Windows and Mac only) |
| F11 | Fullscreen |
| Tab | Toggle Alternate assets |
| Ctrl+R | Reset |

# Project Overview
Ship of Harkinian (SOH) is built atop a custom library dubbed libultraship (LUS). Back in the N64 days, there was an SDK distributed to developers named libultra; LUS is designed to mimic the functionality of libultra on modern hardware. In addition, we are dependant on the source code provided by the OOT decompilation project.

In order for the game to function, you will require a **legally acquired** ROM for Ocarina of Time. Click [here](https://ship.equipment/) to check the compatibility of your specific rom. Any copyrighted assets are extracted from the ROM and reformatted as a .o2r archive file which the code uses.

### Graphics Backends
Currently, there are three rendering APIs supported: DirectX11 (Windows), OpenGL (all platforms), and Metal (MacOS). You can change which API to use in the `Settings` menu of the menubar, which requires a restart.  If you're having an issue with crashing, you can change the API in the `shipofharkinian.json` file by finding the line `gfxbackend:""` and changing the value to `sdl` for OpenGL. DirectX 11 is the default on Windows.

# Custom Assets

Custom assets are packed in `.otr` archive files. To use custom assets, place them in the `mods` folder.

If you're interested in creating and/or packing your own custom asset `.otr` files, check out the following tools:
* [**retro - OTR generator**](https://github.com/HarbourMasters64/retro)
* [**fast64 - Blender plugin**](https://github.com/HarbourMasters/fast64)

# Development
### Building

If you want to manually compile SoH, please consult the [building instructions](docs/BUILDING.md).

### Playtesting
If you want to playtest a continuous integration build, you can find them at the links below. Keep in mind that these are for playtesting only, and you will likely encounter bugs and possibly crashes. 

* [Windows](https://nightly.link/HarbourMasters/Shipwright/workflows/generate-builds/develop/soh-windows.zip)
* [macOS](https://nightly.link/HarbourMasters/Shipwright/workflows/generate-builds/develop/soh-mac.zip)
* [Linux](https://nightly.link/HarbourMasters/Shipwright/workflows/generate-builds/develop/soh-linux.zip)

### Further Reading
More detailed documentation can be found in the 'docs' directory, including the aforementioned [building instructions](docs/BUILDING.md).

* [Credits](docs/CREDITS.md)
* [Custom Music](docs/CUSTOM_MUSIC.md)
* [Controller Mapping](docs/GAME_CONTROLLER_DB.md)
* [Modding](docs/MODDING.md)
* [Versioning](docs/VERSIONING.md)

<a href="https://github.com/Kenix3/libultraship/">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./docs/poweredbylus.darkmode.png">
    <img alt="Powered by libultraship" src="./docs/poweredbylus.lightmode.png">
  </picture>
</a>
