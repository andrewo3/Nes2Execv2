#define ARGS 1
#ifndef ROM_NAME
    #define ROM_NAME argv[1]
    #define ARGS 2
#endif
#ifndef DATAROM
    #define DATAROM placeholder
    #define ARGS 2
#endif
#ifndef DATALENGTH
    #define DATALENGTH 0
#endif

#define SDL_MAIN_HANDLED
#include "SDL2/SDL.h"
#include "GL/glew.h"

#include "rom_data.h"
#include "rom.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "util.h"
#include "shader_data.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "crt_core.h"

#include <cstdio>
#include <string>
#include <csignal>
#include <chrono>
#include <cmath>
#include <thread>
#include <mutex>

//ntsc filter options
static struct CRT crt;
static struct NTSC_SETTINGS ntsc;
static int color = 1;
static int noise = 6;
static int field = 0;
static int raw = 0;
static int hue = 0;


std::mutex interruptedMutex;

const int NES_DIM[2] = {256,240};
int WINDOW_INIT[2];
int filtered_res_scale = 2;
const int FLAGS = SDL_WINDOW_SHOWN|SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE;

volatile sig_atomic_t interrupted = 0;

long long total_ticks;
long long start;
long long start_nano;
double t = 0;
bool fullscreen_toggle = 0;
bool shader_toggle = false;
static int16_t audio_pt = 0;

GLuint shaderProgram;
GLuint vertexShader;
GLuint fragmentShader;

CPU *cpu_ptr;
PPU *ppu_ptr;
APU * apu_ptr;
SDL_AudioDeviceID audio_device;
char* device_name;
SDL_AudioSpec audio_spec;

const int BUFFER_LEN = 1024;

int usage_error() {
    printf("Usage: main rom_filename\n");
    return -1;
}

int invalid_error() {
    printf("Invalid NES rom!\n");
    return -1;
}

void get_filename(char** path) {
    int l = strlen(*path);
    bool end_set = false;
    for (int i=l-1; i>=0; i--) {
        if ((*path)[i]=='.' && !end_set) {
            (*path)[i] = '\0';
            end_set = true;
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
    /*std::FILE* memory_dump = fopen("dump","w");
    fwrite(&cpu_ptr->memory[0x6004],sizeof(uint8_t),strlen((char*)(&cpu_ptr->memory[0x6004])),memory_dump);
    fclose(memory_dump);*/

    interrupted = 1;
    if (signal==SIGSEGV) {
        printf("Segfault!\n");
    }
    exit(EXIT_FAILURE);
}

void viewportBox(int** viewportBox,int width, int height) {
    float aspect_ratio = (float)NES_DIM[0]/NES_DIM[1];
    bool horiz = (float)width/height>aspect_ratio;
    (*viewportBox)[0] = horiz ? (width-aspect_ratio*height)/2.0 : 0;
    (*viewportBox)[1] = horiz ? 0 : (height-width/aspect_ratio)/2.0;
    (*viewportBox)[2] = horiz ? aspect_ratio*height : width;
    (*viewportBox)[3] = horiz ? height : width/aspect_ratio;
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

void CPUThread() {
    while(!interrupted) {
        cpu_ptr->clock();
        total_ticks = cpu_ptr->cycles;
    }
    
}

void PPUThread() {
    while(!interrupted) {
        if (ppu_ptr->cycles<cpu_ptr->cycles*3) {
            ppu_ptr->cycle();
            if (ppu_ptr->debug) {
                printf("PPU REGISTERS: ");
                printf("VBLANK: %i, PPUCTRL: %02x, PPUMASK: %02x, PPUSTATUS: %02x, OAMADDR: N/A (so far), PPUADDR: %04x\n",ppu_ptr->vblank, (uint8_t)cpu_ptr->memory[0x2000],(uint8_t)cpu_ptr->memory[0x2001],(uint8_t)cpu_ptr->memory[0x2002],ppu_ptr->v);
                printf("scanline: %i, cycle: %i\n",ppu_ptr->scanline,ppu_ptr->scycle);
            }
        }
    }
}

void NESLoop() {
    
    //emulator loop
    while (!interrupted) {
        if (cpu_ptr->emulated_clock_speed()<=cpu_ptr->CLOCK_SPEED) { //limit clock speed
            cpu_ptr->clock();
            // 3 dots per cpu cycle
            while (apu_ptr->frames<cpu_ptr->cycles*240/(cpu_ptr->CLOCK_SPEED)) {
                apu_ptr->cycle();
            }
            while (ppu_ptr->cycles<(cpu_ptr->cycles*3)) {
                ppu_ptr->cycle();
                if (ppu_ptr->debug) {
                    printf("PPU REGISTERS: ");
                    printf("VBLANK: %i, PPUCTRL: %02x, PPUMASK: %02x, PPUSTATUS: %02x, OAMADDR: N/A (so far), PPUADDR: %04x\n",ppu_ptr->vblank, (uint8_t)cpu_ptr->memory[0x2000],(uint8_t)cpu_ptr->memory[0x2001],(uint8_t)cpu_ptr->memory[0x2002],ppu_ptr->v);
                    printf("scanline: %i, cycle: %i\n",ppu_ptr->scanline,ppu_ptr->scycle);
                }
                //printf("%i\n",ppu.v);
            }
            total_ticks = cpu_ptr->cycles;
        }
        
    }
    
}

void AudioLoop(void* userdata, uint8_t* stream, int len) {
    for (int i=0; i<len; i+=2) {
        int16_t audio_pt=mix(apu_ptr);
        audio_pt = 0;
        //int16_t v = (int16_t)(16384*sin(t));
        stream[i] = audio_pt&0xff;
        stream[i+1] = (audio_pt>>8)&0xff;
        //stream[i] = 0;
        //stream[i+1] = 0;
        //t+=(2.0*M_PI*500.0)/audio_spec.freq;

    }
}

int main(int argc, char ** argv) {
    std::signal(SIGINT,quit);
    std::signal(SIGSEGV,quit);
    if (argc!=ARGS) {
        return usage_error();
    }
    ROM rom;
    if (ARGS==2) { //nothing specified to replace at compile time
        rom.load_file(ROM_NAME);
        if (!rom.is_valid()) {
            return invalid_error();
        }
    } else {
        unsigned char* placeholder; // if DATAROM not defined - should never actually be used.
        rom.load_arr(DATALENGTH,DATAROM);
        if (!rom.is_valid()) {
            return invalid_error();
        }
    }

    printf("Mapper: %i\n",rom.get_mapper()); //https://www.nesdev.org/wiki/Mapper
    printf("Mirrormode: %i\n",rom.mirrormode);

    char* filename = new char[strlen(ROM_NAME)+1];
    char* original_start = filename;
    memcpy(filename,ROM_NAME,strlen(ROM_NAME)+1);
    get_filename(&filename);

    // SDL initialize
    SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_JOYSTICK);
    SDL_ShowCursor(0);
    int controller_index = 0;
    controller = SDL_JoystickOpen(controller_index);
    auto c_name = SDL_JoystickNameForIndex(controller_index);
    printf("%s\n",c_name ? c_name : "null");
    printf("SDL Initialized\n");

    //set display dimensions
    SDL_DisplayMode DM;
    SDL_GetDesktopDisplayMode(0,&DM);
    WINDOW_INIT[0] = DM.w/2;
    WINDOW_INIT[1] = DM.h/2;

    //audio
    audio_spec.samples = BUFFER_LEN;
    audio_spec.freq = 44100;
    audio_spec.format = AUDIO_S16SYS;  // 16-bit signed, little-endian
    audio_spec.channels = 1;            // Mono
    audio_spec.samples = BUFFER_LEN;
    //audio_spec.size = audio_spec.samples * sizeof(int16_t) * audio_spec.channels;
    audio_spec.callback = AudioLoop;
    audio_device = SDL_OpenAudioDevice(device_name,0,&audio_spec,nullptr,0);
    //stream = SDL_NewAudioStream(AUDIO_U8,1,44100,);
    printf("SDL audio set up.\n");

    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0"); // for linux
    
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );
    SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, 3);
    int* viewport = new int[4];
    viewportBox(&viewport,WINDOW_INIT[0],WINDOW_INIT[1]);
    SDL_Window* window = SDL_CreateWindow(filename,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,WINDOW_INIT[0],WINDOW_INIT[1],FLAGS);
    printf("Window Created\n");
    SDL_GLContext context = SDL_GL_CreateContext(window);
    glewExperimental = GL_TRUE;
    glewInit();
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    printf("OpenGL Initialized.\n");
    GLenum error = glGetError();
    SDL_Event event;
    
    // Shader init
    init_shaders();
    printf("Shaders compiled and linked.\n");

    //ntsc filter init

    /* pass it the buffer to be drawn on screen */
    unsigned char * filtered = (unsigned char*)malloc(NES_DIM[0]*NES_DIM[1]*filtered_res_scale*filtered_res_scale*3*sizeof(char));
    crt_init(&crt, NES_DIM[0]*filtered_res_scale,NES_DIM[1]*filtered_res_scale, CRT_PIX_FORMAT_RGB, &filtered[0]);
    /* specify some settings */
    crt.blend = 0;
    crt.scanlines = 1;


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

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, NES_DIM[0],NES_DIM[1], 0, GL_RGB, GL_UNSIGNED_BYTE, out_img);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenerateMipmap(GL_TEXTURE_2D);

    glUniform1i(glGetUniformLocation(shaderProgram, "textureSampler"), 0);
    glUniform1f(glGetUniformLocation(shaderProgram, "iTime"), (float)(epoch()-start));
    glUniform1f(glGetUniformLocation(shaderProgram, "enabled"), false);
    
    printf("Window texture bound and mapped.\n");

    //Enter NES logic loop alongside window loop
    start = epoch();
    start_nano = epoch_nano();

    CPU cpu(false);
    cpu_ptr = &cpu;
    printf("CPU Initialized.\n");
    static APU apu;
    apu.sample_rate = audio_spec.freq;
    cpu.apu = &apu;
    apu_ptr = &apu;
    apu.cpu = &cpu;
    printf("APU set");

    cpu.loadRom(&rom);
    printf("ROM loaded into CPU.\n");

    PPU ppu(&cpu);
    ppu_ptr = &ppu;
    ppu.debug = false;
    printf("PPU Initialized\n");
    std::thread NESThread(NESLoop);
    //std::thread tCPU(CPUThread);
    //std::thread tPPU(PPUThread);
    //std::thread AudioThread(AudioLoop);

    printf("NES thread started. Starting main window loop...\n");
    float t_time = SDL_GetTicks()/1000.0;
    float last_time = SDL_GetTicks()/1000.0;
    int16_t buffer[BUFFER_LEN*2];
    SDL_PauseAudioDevice(audio_device,0);
    //main window loop
    while (!interrupted) {
        float diff = t_time-last_time;
        last_time = SDL_GetTicks()/1000.0;
        char * new_title = new char[255];
        sprintf(new_title,"%s - %.02f FPS",filename,1/diff);
        SDL_SetWindowTitle(window,new_title);
        // event loop
        while(SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_QUIT:
                    quit(0);
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        int* new_viewport = new int[4];
                        int new_width = event.window.data1;
                        int new_height = event.window.data2;
                        viewportBox(&new_viewport,new_width,new_height);
                        printf("%i %i %i %i\n",new_viewport[0],new_viewport[1],new_viewport[2],new_viewport[3]);
                        glViewport(new_viewport[0],new_viewport[1],new_viewport[2],new_viewport[3]);
                        //glViewport(0,0,new_width,new_height);
                        delete[] new_viewport;
                    }
                    break;
                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                        case SDLK_F11:
                            {
                            fullscreen_toggle = fullscreen_toggle ? false : true;
                            SDL_SetWindowFullscreen(window,fullscreen_toggle*SDL_WINDOW_FULLSCREEN_DESKTOP);
                            SDL_SetWindowSize(window,WINDOW_INIT[0]/2,WINDOW_INIT[1]/2);
                            SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED);
                            int* new_viewport = new int[4];
                            viewportBox(&new_viewport,WINDOW_INIT[0]/2,WINDOW_INIT[1]/2);
                            printf("%i %i %i %i\n",new_viewport[0],new_viewport[1],new_viewport[2],new_viewport[3]);
                            glViewport(new_viewport[0],new_viewport[1],new_viewport[2],new_viewport[3]);
                            delete[] new_viewport;
                            break;
                            }
                        case SDLK_d:
                            cpu.debug = cpu.debug ? false : true;
                            break;
                        case SDLK_s:
                            {
                            shader_toggle = shader_toggle ? false : true;
                            glBindTexture(GL_TEXTURE_2D, texture);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, shader_toggle ? GL_LINEAR : GL_NEAREST);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, shader_toggle ? GL_LINEAR : GL_NEAREST);
                            GLfloat new_vertices[] = {
                                -1.0f, 1.0f,0.0f,0.0f,
                                -1.0f, -1.0f,0.0f,1-0.04f*shader_toggle,
                                1.0f, -1.0f,1.0f,1-0.04f*shader_toggle,
                                1.0f, 1.0f, 1.0f,0.0f
                            };
                            memcpy(vertices,new_vertices,16*sizeof(GLfloat));
                            glBindBuffer(GL_ARRAY_BUFFER,VBO);
                            glBufferData(GL_ARRAY_BUFFER,sizeof(vertices), vertices, GL_STATIC_DRAW);
                            glBindBuffer(GL_ARRAY_BUFFER,0);
                            break;
                            }
                    }
            }
        }
        //logic is executed in nes thread


        //apply ntsc filter before drawing
        if (shader_toggle) {
            ntsc.data = out_img; /* buffer from your rendering */
            ntsc.format = CRT_PIX_FORMAT_RGB;
            ntsc.w = NES_DIM[0];
            ntsc.h = NES_DIM[1];
            ntsc.as_color = color;
            ntsc.field = field & 1;
            ntsc.raw = raw;
            ntsc.hue = hue;
            if (ntsc.field == 0) {
            ntsc.frame ^= 1;
            }
            crt_modulate(&crt, &ntsc);
            crt_demodulate(&crt, noise);
            field ^= 1;

            //render texture from nes (temporarily test_image.jpg)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, NES_DIM[0]*filtered_res_scale,NES_DIM[1]*filtered_res_scale, 0, GL_RGB, GL_UNSIGNED_BYTE, filtered);
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, NES_DIM[0],NES_DIM[1], 0, GL_RGB, GL_UNSIGNED_BYTE, out_img);
        }

        glUseProgram(shaderProgram);
        glUniform1f(glGetUniformLocation(shaderProgram, "iTime"), (float)(epoch()-start));
        glUniform1f(glGetUniformLocation(shaderProgram, "enabled"), false);

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
        t_time = SDL_GetTicks()/1000.0;
    }
    NESThread.join();
    glDetachShader(shaderProgram,vertexShader);
    glDetachShader(shaderProgram,fragmentShader);
    glDeleteProgram(shaderProgram);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_CloseAudioDevice(audio_device);
    SDL_Quit();
    free(filtered);
    delete[] original_start;
    //tCPU.join();
    //tPPU.join();
    //stbi_image_free(img);
    return 0;
}