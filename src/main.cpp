#define SDL_MAIN_HANDLED
#include "SDL2/SDL.h"
#include "GL/glew.h"

#include "rom.h"
#include "cpu.h"
#include "ppu.h"
#include "util.h"
#include "shader_data.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cstdio>
#include <string>
#include <csignal>
#include <chrono>
#include <cmath>
#include <thread>
#include <mutex>

std::mutex interruptedMutex;

const int NES_DIM[2] = {256,240};
const int FLAGS = SDL_WINDOW_SHOWN|SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE;

volatile sig_atomic_t interrupted = 0;

long long total_ticks;
long long start;
long long start_nano;

GLuint shaderProgram;
GLuint vertexShader;
GLuint fragmentShader;

CPU *cpu_ptr;
SDL_AudioDeviceID audio_device;
char* device_name;
SDL_AudioSpec audio_spec;

const int BUFFER_LEN = 1024;

int usage_error() {
    printf("Usage: nes rom_filename\n");
    return -1;
}

int invalid_error() {
    printf("Invalid NES rom!\n");
    return -1;
}

void get_filename(char** path) {
    int l = strlen(*path);
    for (int i=l-1; i>=0; i--) {
        if ((*path)[i]=='.') {
            (*path)[i] = '\0';
        } else if ((*path)[i]=='/' || (*path)[i]=='\\') {
            *path = &(*path)[i+1];
            return;
        }
    }
}

void quit(int signal) {
    std::lock_guard<std::mutex> lock(interruptedMutex);
    printf("Emulated Clock Speed: %li - Target: (approx.) 1789773 - %.02f%% similarity\n",total_ticks/(epoch()-start)*1000,total_ticks/(epoch()-start)*1000/1789773.0*100);
    //for test purpose: remove once done testing!!
    std::FILE* memory_dump = fopen("dump","w");
    fwrite(&cpu_ptr->memory[0x6004],sizeof(uint8_t),strlen((char*)(&cpu_ptr->memory[0x6004])),memory_dump);
    fclose(memory_dump);

    interrupted = 1;
    if (signal==SIGSEGV) {
        printf("Segfault!\n");
    }
    exit(EXIT_FAILURE);
}

void init_shaders() {
    GLint success;
    GLchar infoLog[512];

    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    char * vertex_source = new char[vertex_len+1];
    vertex_source[vertex_len] = 0;
    memcpy(vertex_source,vertex,vertex_len);

    glShaderSource(vertexShader,1,&vertex_source, NULL);
    glCompileShader(vertexShader);

    GLint compileStatus;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &compileStatus);

    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    char * fragment_source = new char[fragment_len+1];
    fragment_source[fragment_len] = 0;
    memcpy(fragment_source,fragment,fragment_len);

    glShaderSource(fragmentShader,1,&fragment_source, NULL);
    glCompileShader(fragmentShader);

    //Check vertex shader compilation
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        fprintf(stderr, "Vertex Shader Compilation Failed:\n%s\n", infoLog);
    }

    // Check fragment shader compilation
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        fprintf(stderr, "Fragment Shader Compilation Failed:\n%s\n", infoLog);
    }

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &compileStatus);

    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        fprintf(stderr, "Shader Program Linking Failed:\n%s\n", infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    delete[] vertex_source;
    delete[] fragment_source;
}

void NESLoop(ROM* r_ptr) {
    printf("Mapper: %i\n",r_ptr->get_mapper()); //https://www.nesdev.org/wiki/Mapper
    printf("Mirrormode: %i\n",r_ptr->mirrormode);
    CPU cpu(false);
    cpu_ptr = &cpu;
    printf("CPU Initialized.\n");

    cpu.loadRom(r_ptr);
    printf("ROM loaded into CPU.\n");

    PPU ppu(&cpu);
    ppu.debug = false;
    printf("PPU Initialized\n");
    
    //emulator loop
    while (!interrupted) {
        cpu.clock();
        // 3 dots per cpu cycle
        while (ppu.cycles<cpu.cycles*3) {
            ppu.cycle();
            if (ppu.debug) {
                printf("PPU REGISTERS: ");
                printf("VBLANK: %i, PPUCTRL: %02x, PPUMASK: %02x, PPUSTATUS: %02x, OAMADDR: N/A (so far), PPUADDR: %04x\n",ppu.vblank, (uint8_t)cpu.memory[0x2000],(uint8_t)cpu.memory[0x2001],(uint8_t)cpu.memory[0x2002],ppu.v);
                printf("scanline: %i, cycle: %i\n",ppu.scanline,ppu.scycle);
            }
            //printf("%i\n",ppu.v);
        }
        total_ticks = cpu.cycles;
        
    }
    
}

void AudioLoop() {
    int16_t buffer[BUFFER_LEN*2];
    int16_t copy[BUFFER_LEN*2];
    SDL_PauseAudioDevice(audio_device,0);
    printf("SAMPLES: %i\n",audio_spec.samples);
    printf("FREQUENCY: %i\n",audio_spec.freq);
    printf("CHANNELS: %i\n",audio_spec.channels);
    printf("SILENCE: %i\n",audio_spec.silence);
    printf("SIZE: %i\n",audio_spec.size);
    printf("FORMAT: %i\n",audio_spec.format);
    //long long x=0;
    double t_time = SDL_GetTicks()/1000.0;
    while (!interrupted) {
        //test audio
        //printf("Buffer: [");
        //double t_time = (epoch_nano()-start_nano)/1000000000.0f;
        
        //printf("%f\n",t_time);
        for (int i=0; i<BUFFER_LEN; i++) {
            double t = t_time+(double)i/audio_spec.freq;
            double s_value = sin(t*2*M_PI*500.0);
            //printf("%lf,",s_value);
            buffer[2*i]=(int16_t)(s_value*((1<<15)-1));
            buffer[2*i+1]=(int16_t)(s_value*((1<<15)-1));
            //x++;
        }
        //printf("]\n");
        SDL_QueueAudio(audio_device,buffer,BUFFER_LEN*2*sizeof(int16_t));
        //SDL_PauseAudioDevice(audio_device,1);
        t_time = SDL_GetTicks()/1000.0;
        //int delay = 1000;
        //SDL_Delay(delay);
        
    }
}

int main(int argc, char ** argv) {
    std::signal(SIGINT,quit);
    std::signal(SIGSEGV,quit);
    if (argc!=2) {
        return usage_error();
    }
    ROM rom(argv[1]);
    if (!rom.is_valid()) {
        return invalid_error();
    }
    char* filename = new char[strlen(argv[1])+1];
    char* original_start = filename;
    memcpy(filename,argv[1],strlen(argv[1])+1);
    get_filename(&filename);

    int dim[3] = {256,240,3};
    //GLubyte* img = stbi_load("res/test_image2.jpg", &dim[0],&dim[1],&dim[2],STBI_default);
    printf("%i %i %i\n",dim[0],dim[1],dim[2]);
    // SDL initialize
    SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO);
    printf("SDL Initialized\n");
    printf("SAMPLES: %i\n",audio_spec.samples);
    if (SDL_GetDefaultAudioInfo(&device_name,&audio_spec,0)) { //get default output
        printf("Failed to open audio device.");
        return -1;
    }
    audio_spec.samples = BUFFER_LEN;
    audio_spec.size = audio_spec.samples * sizeof(int16_t) * audio_spec.channels;
    audio_device = SDL_OpenAudioDevice(device_name,0,&audio_spec,nullptr,0);
    //stream = SDL_NewAudioStream(AUDIO_U8,1,44100,);
    printf("SDL audio set up.\n");

    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0"); // for linux
    
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );
    SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_Window* window = SDL_CreateWindow(filename,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,NES_DIM[0],NES_DIM[1],FLAGS);
    delete[] original_start;
    printf("Window Created\n");
    SDL_GLContext context = SDL_GL_CreateContext(window);
    glewExperimental = GL_TRUE;
    glewInit();
    glViewport(0, 0, NES_DIM[0], NES_DIM[1]);
    printf("OpenGL Initialized.\n");
    GLenum error = glGetError();
    SDL_Event event;
    
    // Shader init
    init_shaders();
    printf("Shaders compiled and linked.\n");

    //VBO & VAO init for the fullscreen texture
    GLuint VBO, VAO;
    glGenVertexArrays(1,&VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);

    // vertices of quad covering entire screen with tex coords
    GLfloat vertices[] = {
        -1.0f, 1.0f,0.0f,0.0f,
        -1.0f, -1.0f,0.0f,1.0f,
        1.0f, -1.0f,1.0f,1.0f,
        1.0f, 1.0f, 1.0f,0.0f
    };


    glBindBuffer(GL_ARRAY_BUFFER,VBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2*sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    //texture init
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, dim[0],dim[1], 0, GL_RGB, GL_UNSIGNED_BYTE, out_img);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenerateMipmap(GL_TEXTURE_2D);

    glUniform1i(glGetUniformLocation(shaderProgram, "textureSampler"), 0);
    
    printf("Window texture bound and mapped.\n");

    //Enter NES logic loop alongside window loop
    start = epoch();
    start_nano = epoch_nano();
    std::thread NESThread(NESLoop,&rom);
    std::thread AudioThread(AudioLoop);

    printf("NES thread started. Starting main window loop...\n");
    //main window loop
    while (!interrupted) {
        // event loop
        while(SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_QUIT:
                    quit(0);
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        glViewport(0,0,event.window.data1,event.window.data2);
                    }
            }
        }
        //logic is executed in nes thread

        //render texture from nes (temporarily test_image.jpg)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, dim[0],dim[1], 0, GL_RGB, GL_UNSIGNED_BYTE, out_img);

        glUseProgram(shaderProgram);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLE_FAN,0,4);
        glBindVertexArray(0);
        glUseProgram(0);

        //update screen
        SDL_GL_SwapWindow(window);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDetachShader(shaderProgram,vertexShader);
    glDetachShader(shaderProgram,fragmentShader);
    glDeleteProgram(shaderProgram);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_CloseAudioDevice(audio_device);
    SDL_Quit();
    NESThread.join();
    AudioThread.join();
    //stbi_image_free(img);
    return 0;
}