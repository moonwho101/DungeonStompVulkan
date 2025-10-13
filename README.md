# Dungeon Stomp Vulkan

![Dungeon Stomp Vulkan](../main/Textures/screenshot13.jpg)



![Dungeon Stomp Vulkan](../main/Textures/screenshot12.jpg)

## Controls

* Run DungeonStomp.exe from the bin directory to try the game.
* Press 'F' for Fullscreen
* WSAD to move, E to JUMP
* SPACE to open doors
* Q/Z to cycle weapons/spells
* Left mouse button to attack, right mouse button to move forward
* Press F5 to Load game, F6 to Save game

## Debug Controls

* C - Toggle VULKAN console debug window
* G - Toggle gravity (Keypad +/- move up, move down)
* I - Disable music
* P - Random music
* X - Give experience points
* K - Give all weapons and spells
* B - Toggle Camera head bob
* N - Toggle Normal map
* ] - Next Dungeon Level
* [ - Previous Dungeon Level

## DirectX12

Dungeon Stomp for DirectX12, is avaiable [Dungeon Stomp DirectX12](https://github.com/moonwho101/DungeonStompDirectX12).

## Compiling

To build, you'll need Vulkan API.  Compile using Microsoft Visual Studio 2022 Community Edition. For best results compile in 'Release' mode.
Happy Dungeon Stomping - Breeyark!

## Vulkan Examples

Thanks to baeng72 for the C++ Vulkan examples [VulkanIntroD3DPort](https://github.com/baeng72/VulkanIntroD3DPort).

## Dungeon Stomp VULKAN with Physically based rendering (PBR).

* Dungeon Stomp is a VULKAN 3D dungeon game.
* Materials (Diffuse Albedo, Fresnel, Roughness, Metallicness)
* Physically based rendering (PBR)
* Mipmaps
* Normal Maps (specular map in alpha channel)
* Cube Maps (Skybox)
* Shadow Maps
* Fog, Alpha transparency and Alpha testing
* Head bob using two sine waves
* XBOX game controller is supported (you can enable it in DirectInput.cpp)
* Written in Microsoft C++
