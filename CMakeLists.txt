cmake_minimum_required(VERSION 3.28)

include(${CMAKE_CURRENT_LIST_DIR}/tools/rp6502.cmake)

project(RP_CosmicArc_II C CXX ASM)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(ART_PYTHON ${Python3_EXECUTABLE})

add_executable(RP_CosmicArc_II)
rp6502_align(RP_CosmicArc_II "XRAM_(?!IMG_|TILES_|MAP_|SFX_DATA|KEYBOARD|GAMEPAD|TXT$|HUD_|MSG$).*")

function(arc_sheet name w h xram_dest)
    set(out ${CMAKE_CURRENT_BINARY_DIR}/${name}.bin)
    add_custom_command(
        OUTPUT ${out}
        COMMAND ${ART_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/png2bin.py
            ${CMAKE_CURRENT_SOURCE_DIR}/images/${name}.png ${out} ${w} ${h} 4
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/png2bin.py ${CMAKE_CURRENT_SOURCE_DIR}/images/${name}.png
        VERBATIM
    )
    rp6502_asset(RP_CosmicArc_II XRAM(${xram_dest}) ${out})
endfunction()

# 4bpp Sprite & Tile Sheets
arc_sheet(ark          64 32 XRAM_IMG_ARK)
arc_sheet(rock_l       20 20 XRAM_IMG_ROCK_L)
arc_sheet(rock_m       16 16 XRAM_IMG_ROCK_M)
arc_sheet(rock_s        8  8 XRAM_IMG_ROCK_S)
arc_sheet(saucer       16 12 XRAM_IMG_SAUCER)
arc_sheet(raider       16 16 XRAM_IMG_RAIDER)
arc_sheet(gunship      20 20 XRAM_IMG_GUNSHIP)
arc_sheet(missile      12 12 XRAM_IMG_MISSILE)
arc_sheet(pod          12 12 XRAM_IMG_POD)
arc_sheet(shots         8  8 XRAM_IMG_SHOTS)
arc_sheet(boom         16 16 XRAM_IMG_BOOM)
arc_sheet(creatures    12 12 XRAM_IMG_CREATURES)
arc_sheet(cargo        12 12 XRAM_IMG_CARGO)
arc_sheet(beam         16 16 XRAM_IMG_BEAM)
arc_sheet(gun          12  8 XRAM_IMG_GUN)
arc_sheet(pulse        32  4 XRAM_IMG_PULSE)
arc_sheet(tunnel       24 16 XRAM_IMG_TUNNEL)
arc_sheet(tiles_space   8  8 XRAM_TILES_SPACE)
arc_sheet(tiles_planet  8  8 XRAM_TILES_PLANET)
arc_sheet(tiles_hud     8  8 XRAM_TILES_HUD)

# OPL2 SFX Blob
rp6502_asset(RP_CosmicArc_II XRAM(XRAM_SFX_DATA) music/sfx.bin)

# Streamed OPL2 VGM Music Tracks
foreach(track Title Map Space Pirates Planet)
    rp6502_asset(RP_CosmicArc_II ${track}.vgm music/${track}.vgm)
endforeach()

rp6502_executable(RP_CosmicArc_II DATA default RESET default)
rp6502_web(RP_CosmicArc_II)

target_include_directories(RP_CosmicArc_II PRIVATE src)
target_compile_options(RP_CosmicArc_II PRIVATE -Oz -Wall -Wextra)

target_sources(RP_CosmicArc_II PRIVATE
    src/main.c
    src/achieve.c
    src/galaxy.c
    src/input.c
    src/math_fixed.c
    src/music.c
    src/opl.c
    src/planet.c
    src/port.c
    src/save.c
    src/sfx.c
    src/space.c
    src/vgm.c
    src/video.c
)
