## Ignore damp/warm stone plugin for [DFHack](https://github.com/DFHack/dfhack)

This plugin will prevent digging cancellation from occuring for damp or warm stone through 2 commands:
- `toggle-dig-ignore-warm`: Toggle ignoring warm stone when digging
- `toggle-dig-ignore-damp`: Toggle ignoring damp stone when digging

Load or unload the plugin using `enable ignorewarmdampstone` or `disable ignorewarmdampstone` in the DFHack console.

This repository uses submodules so make sure to initialise them by using the `git clone --recursive` or `git submodule init`.

To build the repository, an environment variable named `CPP_SDK_PATH` should point to the directory containing external dependencies.
The minimum dependencies are:
- [DFHack](https://github.com/DFHack/dfhack)
- [Microsoft Detours](https://github.com/microsoft/Detours)

The layout should be like this:
```
$CPP_SDK_PATH
├───dfhack
│	├───Include
│	│   ├───depends
│	│   │   ├───clsocket
│	│   │   │   ...
│	│   │   └───zlib
│	│   ├───df
│	│   │   └───custom
│	│   └───modules
│	├───Release
│	│	└───dfhack-version.lib
│	│	└───SDL.lib
│	└───RelWithDebInfo
│		└───dfhack-version.lib
│		└───SDL.lib
└───detours
	├───include
	└───lib.X64
		└───detours.lib
```

This plugin is experimental and may break at the first update of Dwarf Fortress.
The hook enabling its functionality is installed by detecting a unique pattern (`42??????????????83??7046`) and offset (-96) to find the beginning of the method used by the game.
As an example for the current version, the disassembled code targetted by the pattern is
```
.text:0000000140159FB0 ; int __fastcall checkRevealed(__int64 map_block_in, __int16 i_in, __int16 j_in)

...

.text:000000014015A018                 and     edi, 70h        ; mask_dig
.text:000000014015A01B                 movzx   r9d, word ptr [rcx+r8*2+92h]
.text:000000014015A024                 movzx   ecx, r9w
```