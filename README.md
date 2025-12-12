[comment]: <> (Todo: Make Light Mode Image)
[comment]: <> (Todo: Make Dark Mode Image)

# 2 Ship 2 Harkinian UWP

## Discord

Discord: https://discord.gg/nCyRs4vJAM

If you're having any trouble after reading through this `README`, feel free ask for help in the Xbox Emulation Hub Support text channels. Please keep in mind that we do not condone piracy.

# Quick Start

2Ship does not include any copyrighted assets.  You are required to provide a supported copy of the game.

### 1. Verify your ROM dump
You can verify you have dumped a supported copy of the game by using the compatibility checker at https://2ship.equipment/. If you'd prefer to manually validate your ROM dump, you can cross-reference its `sha1` hash with the hashes [here](docs/supportedHashes.json).

### 2. Download 2 Ship 2 Harkinian from [Releases](https://github.com/SternXD/2ship2harkinian-uwp/releases)

### 3. Launch the Game!
#### Universal Windows Platform (UWP) / Xbox
* Install the `.msix` package.
* Launch the game from the Start menu or Xbox dashboard.
* On first launch, you'll be prompted to select a storage location (LocalState, D:\2ship\, or E:\2ship\).
* Select your supported copy of the game when prompted.
* Wait for asset extraction to complete, then play!

**Note:** The UWP port is an unofficial port maintained by SternXD and originally ported by worleydl. This port is designed for Xbox consoles in Developer Mode.

### 4. Play!

Congratulations, you are now sailing with 2 Ship 2 Harkinian! Have fun!

# Configuration

### Default keyboard configuration (TBD)
| N64 | A | B | Z | Start | Analog stick | C buttons | D-Pad |
| - | - | - | - | - | - | - | - |
| Keyboard | X | C | Z | Space | WASD | Arrow keys | TFGH |

### Other shortcuts (TBD)
| Keys | Action |
| - | - |
| F1 | Toggle menubar |
| F11 | Fullscreen |
| Tab | Toggle Alternate assets |
| Ctrl+R | Reset |

### Graphics Backends
Currently, there are two rendering APIs supported: DirectX 11, and OpenGL. You can change which API to use in the `Settings` menu of the menubar, which requires a restart.

If you're having an issue with crashing, you can also change the API manually in the `2ship2harkinian.json` file by finding the `"Backend": {` section and updating the backend ID and name. Be sure to use one of the valid values:

- `0` = DirectX 11 (default on UWP)
- `1` = OpenGL

# Custom Assets

Custom assets are packed in `.o2r` or `.otr` files. To use custom assets, place them in the `mods` folder.

If you're interested in creating and/or packing your own custom asset `.o2r`/`.otr` files, check out the following tools:
* [**retro - OTR and O2R generator**](https://github.com/HarbourMasters64/retro)
* [**fast64 - Blender plugin (Note that MM is not fully supported at this time)**](https://github.com/HarbourMasters/fast64)

# Development
### Building

If you want to manually compile 2S2H, please consult the [building instructions](docs/BUILDING.md).
