g++ -g -DGLEW_STATIC -static -Iinclude\win32 -Isrc\ntsc-filter -Iinclude\universal -Isrc\imgui -Isrc\imgui\backends src/imgui/backends/*.cpp src/imgui/*.cpp src/ntsc-filter/crt_core.c src/ntsc-filter/crt_ntsc.c src/*.cpp -Llib -static-libgcc -static-libstdc++ -Wl,-Bstatic -lmingw32 -lSDL2main -lSDL2 -lm -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8 -lglew32s -lopengl32 -o bin/main