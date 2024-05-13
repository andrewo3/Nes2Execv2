clang++ -g -std=c++17 -F./bin -L./lib -Isrc/imgui -Isrc/imgui/backends -Iinclude/unix -Isrc/ntsc-filter -Iinclude -Iinclude/universal src/imgui/*.cpp src/imgui/backends/*.cpp src/ntsc-filter/crt_core.c src/ntsc-filter/crt_ntsc.c src/*.cpp -framework SDL2 -framework OpenGL -lGLEW -o bin/main