xxd -n fragment -i res/shaders/main.frag > include/universal/shader_data.h
xxd -n vertex -i res/shaders/main.vert >> include/universal/shader_data.h
g++ src/ntsc-filter/crt_core.c src/util.cpp src/rom.cpp src/cpu.cpp src/cpu_helper.cpp src/ppu.cpp src/apu.cpp -g src/main.cpp -Iinclude/universal -Isrc/ntsc-filter -lSDL2 -lGL -lGLEW -lGLU -o "bin/main"