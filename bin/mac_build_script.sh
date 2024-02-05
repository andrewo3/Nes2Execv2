xxd -n fragment -i res/shaders/main.frag > include/universal/shader_data.h
xxd -n vertex -i res/shaders/main.vert >> include/universal/shader_data.h
g++-11 -g -F./bin -L./lib -Iinclude/unix -Isrc/ntsc-filter -Iinclude/universal src/ntsc-filter/crt_core.c src/ntsc-filter/crt_ntsc.c src/util.cpp src/rom.cpp src/cpu.cpp src/cpu_helper.cpp src/ppu.cpp src/apu.cpp src/mapper.cpp src/main.cpp -framework SDL2 -framework OpenGL -lGLEW -o bin/main
#g++-11 -v src/util.cpp src/rom.cpp src/cpu.cpp src/cpu_helper.cpp src/ppu.cpp src/apu.cpp -g src/main.cpp -I./include/universal -I./include/unix -L./lib -F./bin -framework SDL2 -framework OpenGL -lGLEW -o /Users/andrewogundimu/code_projects/NES/bin/main