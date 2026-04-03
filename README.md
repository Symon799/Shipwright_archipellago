![Ship of Harkinian](docs/shiptitle.darkmode.png#gh-dark-mode-only)
![Ship of Harkinian](docs/shiptitle.lightmode.png#gh-light-mode-only)

This is a fork of jeromkiller/Shipwright_archipellago that adds an auto Map Tracker

It is a version of Ocarina of Time Ship of Harkinian with an auto MapTracker that aims to welcome new players to be able to enjoy the archipelago randomizer.

You can open the MapTracker using : Esc -> Randomizer -> Check Tracker -> Toggle Check Tracker

MapTracker discord Thread : https://discord.com/channels/731205301247803413/1481299118264680448

## Map Tracker
This map Tracker replaces the Check Tracker imgui window (Still accessible) and adds all of the maps with every check on it. It is updated in real time when you change zone, when you do a check or when the time of day or age of Link changes.

<img width="1260" height="845" alt="Hyrule" src="https://github.com/user-attachments/assets/ead492f2-fa17-424d-be42-9320db7d5077" />
<img width="1606" height="887" alt="Kokiri" src="https://github.com/user-attachments/assets/8e29efe7-271b-4eb9-954c-d8f8f7a93e86" />
<img width="1433" height="874" alt="HyruleField" src="https://github.com/user-attachments/assets/40a4354d-eb36-43e8-b3ab-46b3885989cb" />
<img width="1285" height="996" alt="Lost Woods" src="https://github.com/user-attachments/assets/1f4c0db7-2e7e-4c2e-b523-bb1429184f1a" />
<img width="1162" height="1082" alt="FireTemple" src="https://github.com/user-attachments/assets/4a9aa93f-4a4d-441e-934a-2771cb6dc170" />

## Time and Age requirements
When a check is Yellow it means you can do the check but you need to change the time or the age. Hover on the check to see the requirements

<img width="445" height="157" alt="Capture d’écran 2026-03-03 132536" src="https://github.com/user-attachments/assets/a00b5ed8-733d-4caf-84c0-665e4d061ca1" />

## Link to other Zones
Link to other zones are round and if the outline of the circle is red, you can't go to that zone, if it is yellow you need to change Age or Time
<img width="593" height="520" alt="image" src="https://github.com/user-attachments/assets/811f1441-f569-4e0b-ab3b-c87319a32bd1" />

## Hints
There is also a hint system that adds hints on some of the checks. (witch can be edited in the tool editor mentionned just below) There is not many hints at the moment but it will improve over time.

<img width="372" height="152" alt="Capture d’écran 2026-03-03 132951" src="https://github.com/user-attachments/assets/e4acf165-7853-43c3-b561-063216d286e8" />
<img width="414" height="159" alt="Capture d’écran 2026-03-03 133019" src="https://github.com/user-attachments/assets/09f3fa5d-cfae-47bf-abb7-ec30209b6d97" />

## Ressources
The Map tracker works with a ressource pack that can be edited using this python tool : https://github.com/Symon799/SoH_Map_Ressource_Maker

The Ressource pack is in the mods/check_tracker_map_pack folder

If a check is missing, or not at the right place or you want to add a hint you can edit the ressource pack using this tool

There is probably still some wrong or misplaced checks in the ressource pack so feel free to contact me if you want to contribute to it !
It would be very apprieciated if you could play the latest build and report if some things when wrong or could be improved since it takes a lot of time to test it fully.

Big thank you to my great friend Titrok with whom I made this auto Map Tracker <3

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
