#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

#include <SDL.h>

// #define ENTRY_POINT 0x200 // entry point for ROM
#define DISPLAY_WIDTH 64
#define DISPLAY_HEIGHT 32

const uint8_t font[] = {
  0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
  0x20, 0x60, 0x20, 0x20, 0x70, // 1
  0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
  0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
  0x90, 0x90, 0xF0, 0x10, 0x10, // 4
  0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
  0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
  0xF0, 0x10, 0x20, 0x40, 0x40, // 7
  0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
  0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
  0xF0, 0x90, 0xF0, 0x90, 0x90, // A
  0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
  0xF0, 0x80, 0x80, 0x80, 0xF0, // C
  0xE0, 0x90, 0x90, 0x90, 0xE0, // D
  0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
  0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

typedef enum {
  QUIT,
  RUNNING,
  STOPPED
} e_state_t;

struct rgba_s {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
} const rgba_default = { 0x00, 0x00, 0x00, 0xFF };
typedef struct rgba_s rgba_t;

// CHIP8 instruction
typedef struct {
  uint16_t opcode;
  uint16_t NNN; // address/constant
  uint8_t NN;   // 8 bit constant
  uint8_t N;    // 4 bit constant
  uint8_t X;    // register
  uint8_t Y;    // register
} CHIP8_instruction_t;

struct CHIP8_s {
  uint16_t window_w;
  uint16_t window_h;
  uint8_t window_scale;
  rgba_t bg_color;
  rgba_t fg_color;
  uint32_t clock_rate;
  e_state_t run_state;
  SDL_Window *Window;
  SDL_Renderer *Renderer;
  SDL_Event *last_event;
  char *rom;          // name of currently running program, argv[1]
  uint8_t MM[0x1000]; // main memory up to 4K
  uint8_t V[0x10];    // 16 general purpose registers
  uint16_t PC;        // program counter
  uint16_t I;         // index register
  uint8_t S;          // sound timer
  uint8_t D;          // delay timer
  bool display[DISPLAY_WIDTH*DISPLAY_HEIGHT]; // display
  uint16_t stack[12]; // The stack, mapped to RAM, for 12 levels of call nesting according to PG36 COSMAC VIP manual
  uint8_t SP;
  bool keypad[0x10];  // inputs 0-F
  CHIP8_instruction_t instruction; // current instruction
} const CHIP8_default = { 
  DISPLAY_WIDTH, 
  DISPLAY_HEIGHT, 20, rgba_default, rgba_default, 700, STOPPED, NULL, NULL, NULL, "", {0}, {0}, 0, 0, 0, 0, {0}, {0}, 0, {0}, {0} };
typedef struct CHIP8_s CHIP8_t;

void CHIP8_main_loop(CHIP8_t *chip8_i);
void CHIP8_emulate_instruction(CHIP8_t *chip8_i);

CHIP8_t* CHIP8_create(uint8_t scale_factor, uint8_t clock_rate, uint32_t bg_color, uint32_t fg_color) {
  // need to copy default so we don't mutate the default structure instance
  // CHIP8_t *chip8_i = (CHIP8_t *)malloc(sizeof(CHIP8_t));
  CHIP8_t *chip8_i = malloc(sizeof(CHIP8_t));
  memcpy(chip8_i, &CHIP8_default, sizeof(CHIP8_t));
  if (scale_factor) chip8_i->window_scale = scale_factor;
  if (clock_rate) chip8_i->clock_rate = clock_rate;
  if (bg_color) {
    chip8_i->bg_color.r = (bg_color >> 24) & 0xFF; // shift right 24 and take the byte because of x86 endianness
    chip8_i->bg_color.g = (bg_color >> 16) & 0xFF; // since x86 is little endian, r is the MSB stored at highest address of the uint32 word
    chip8_i->bg_color.b = (bg_color >> 8) & 0xFF;
    chip8_i->bg_color.a = (bg_color >> 0) & 0xFF; // for OCD's sake :)
  }
  if (fg_color) {
    chip8_i->fg_color.r = (fg_color >> 24) & 0xFF; // shift right 24 and take the byte because of x86 endianness
    chip8_i->fg_color.g = (fg_color >> 16) & 0xFF; // since x86 is little endian, r is the MSB stored at highest address of the uint32 word
    chip8_i->fg_color.b = (fg_color >> 8) & 0xFF;
    chip8_i->fg_color.a = (fg_color >> 0) & 0xFF; // for OCD's sake :)
  }
  chip8_i->Window = SDL_CreateWindow(
    "Dev's CHIP8 Emulator Instance",
    SDL_WINDOWPOS_UNDEFINED,
    SDL_WINDOWPOS_UNDEFINED,
    chip8_i->window_w*chip8_i->window_scale,
    chip8_i->window_h*chip8_i->window_scale,
    0
  );

  if (chip8_i->Window == NULL) {
    SDL_Log("SDL could not create window: %s\n", SDL_GetError());
    return NULL;
  }

  chip8_i->Renderer = SDL_CreateRenderer(chip8_i->Window, -1, SDL_RENDERER_ACCELERATED);
  if (!chip8_i->Renderer) {
    SDL_Log("SDL could not create SDL renderer: %s\n", SDL_GetError());
    return NULL;
  }
  SDL_SetRenderDrawColor(chip8_i->Renderer, chip8_i->bg_color.r, chip8_i->bg_color.g, chip8_i->bg_color.b, chip8_i->bg_color.a);
  SDL_RenderClear(chip8_i->Renderer);
  SDL_RenderPresent(chip8_i->Renderer);
  return chip8_i;
}

void CHIP8_destroy(CHIP8_t *chip8_i) {
  SDL_DestroyRenderer(chip8_i->Renderer);
  SDL_DestroyWindow(chip8_i->Window);
  free(chip8_i);
}

int CHIP8_init(CHIP8_t *chip8_i, char *rom_name) {
  memcpy(chip8_i->MM, font, sizeof(font)); // must be in first 512 bytes https://tobiasvl.github.io/blog/write-a-chip-8-emulator/
  chip8_i->rom = rom_name;

  FILE *rom = fopen(rom_name, "rb");
  if (!rom) {
    SDL_Log("Failed to read ROM");
    return -1;
  }
  fseek(rom, 0, SEEK_END);
  const size_t size = ftell(rom);
  rewind(rom);
  if (fread(&chip8_i->MM[0x200], size, 1, rom) != 1) {
    SDL_Log("Could not read ROM");
    return -1;
  }
  fclose(rom);

  #ifdef DEBUGROM
    printf("Initializing CHIP8 instance..\n");
    printf("Loaded ROM:\n" );
    for (int i = 0; i < 0x400; i++) {
      printf("\t0x%04X : 0x%02X\n", i, chip8_i->MM[i]);
    }
  #endif

  chip8_i->PC = 0x200;
  chip8_i->SP = 0;
  chip8_i->run_state = STOPPED;
  return 0;
}

void CHIP8_start(CHIP8_t *chip8_i) {
  chip8_i->run_state = RUNNING;
  CHIP8_main_loop(chip8_i);
}

int CHIP8_stop(CHIP8_t *chip8_i) {
  chip8_i->run_state = STOPPED;
  return 0;
}

// keypad keyboard
// 123C   1234 
// 456D   qwer
// 789E   asdf
// A0BF   zxcv

void CHIP8_handle_input(CHIP8_t *chip8_i) {
  SDL_Event event;
  // TODO need to get information from the event to see which instance this is, but won't make a difference for now
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    // SDL_Quit fires when X is clicked in the windows toolbar for the program
      case SDL_QUIT: 
        chip8_i->run_state = QUIT;
        break;
      case SDL_KEYDOWN:
        switch (event.key.keysym.sym) {
          case SDLK_SPACE: // use as pause
            if (chip8_i->run_state == RUNNING) {
              chip8_i->run_state = STOPPED; // pause
              SDL_Log("<<<<< PAUSED >>>>>");
            } else {
              chip8_i->run_state = RUNNING; // resume
              SDL_Log("<<<<< RESUME >>>>>");
            }
            break;
          case SDLK_1: chip8_i->keypad[0x1] = true; break;
          case SDLK_2: chip8_i->keypad[0x2] = true; break;
          case SDLK_3: chip8_i->keypad[0x3] = true; break;
          case SDLK_4: chip8_i->keypad[0xC] = true; break;
          case SDLK_q: chip8_i->keypad[0x4] = true; break;
          case SDLK_w: chip8_i->keypad[0x5] = true; break;
          case SDLK_e: chip8_i->keypad[0x6] = true; break;
          case SDLK_r: chip8_i->keypad[0xD] = true; break;
          case SDLK_a: chip8_i->keypad[0x7] = true; break;
          case SDLK_s: chip8_i->keypad[0x8] = true; break;
          case SDLK_d: chip8_i->keypad[0x9] = true; break;
          case SDLK_f: chip8_i->keypad[0xE] = true; break;
          case SDLK_z: chip8_i->keypad[0xA] = true; break;
          case SDLK_x: chip8_i->keypad[0x0] = true; break;
          case SDLK_c: chip8_i->keypad[0xB] = true; break;
          case SDLK_v: chip8_i->keypad[0xF] = true; break;
        }
        break;
      case SDL_KEYUP:
        switch (event.key.keysym.sym) {
          case SDLK_1: chip8_i->keypad[0x1] = false; break;
          case SDLK_2: chip8_i->keypad[0x2] = false; break;
          case SDLK_3: chip8_i->keypad[0x3] = false; break;
          case SDLK_4: chip8_i->keypad[0xC] = false; break;
          case SDLK_q: chip8_i->keypad[0x4] = false; break;
          case SDLK_w: chip8_i->keypad[0x5] = false; break;
          case SDLK_e: chip8_i->keypad[0x6] = false; break;
          case SDLK_r: chip8_i->keypad[0xD] = false; break;
          case SDLK_a: chip8_i->keypad[0x7] = false; break;
          case SDLK_s: chip8_i->keypad[0x8] = false; break;
          case SDLK_d: chip8_i->keypad[0x9] = false; break;
          case SDLK_f: chip8_i->keypad[0xE] = false; break;
          case SDLK_z: chip8_i->keypad[0xA] = false; break;
          case SDLK_x: chip8_i->keypad[0x0] = false; break;
          case SDLK_c: chip8_i->keypad[0xB] = false; break;
          case SDLK_v: chip8_i->keypad[0xF] = false; break;
        }
        break;
      default: break;
    }
  }
}

// can be used for threading later, for now just call
void CHIP8_main_loop(CHIP8_t *chip8_i) {
  uint64_t cycle_start, cycle_end, delay;
  double elapsed;
  while (chip8_i->run_state != QUIT) {
    // start time
    cycle_start = SDL_GetPerformanceCounter();

    CHIP8_handle_input(chip8_i);
    if (chip8_i->run_state == STOPPED) continue;

    // target clock rate is achieved by performing 1 60th of the instructions per second per iteration, with 60 Hz main loop
    for (uint32_t i = 0; i < chip8_i->clock_rate / 60; i++) {
      CHIP8_emulate_instruction(chip8_i);
    }

    if (chip8_i->PC > 0x1000) {
      printf("\tFATAL ERROR: PC went out of bounds\n");
      break;
    }

    // update display
    SDL_Rect pixel = {.x = 0, .y = 0, .w = chip8_i->window_scale, .h = chip8_i->window_scale};
    for (uint32_t i = 0; i < sizeof(chip8_i->display); i++) {
      // express 1D coordinates as 2D parametric coordinates
      pixel.x = (i % DISPLAY_WIDTH)*chip8_i->window_scale;
      pixel.y = (i / DISPLAY_WIDTH)*chip8_i->window_scale;

      if (chip8_i->display[i]) {
        // foreground
        SDL_SetRenderDrawColor(chip8_i->Renderer, chip8_i->fg_color.r, chip8_i->fg_color.g, chip8_i->fg_color.b, chip8_i->fg_color.a);

      } else {
        // background
        SDL_SetRenderDrawColor(chip8_i->Renderer, chip8_i->bg_color.r, chip8_i->bg_color.g, chip8_i->bg_color.b, chip8_i->bg_color.a);
      }
      SDL_RenderFillRect(chip8_i->Renderer, &pixel);
      #ifdef DEBUG
        SDL_SetRenderDrawColor(chip8_i->Renderer, 0x80, 0x80, 0x80, 0x80);
        SDL_RenderDrawRect(chip8_i->Renderer, &pixel);
      #endif
    }
    SDL_RenderPresent(chip8_i->Renderer);
    // update timers
    if (chip8_i->D > 0) {
      chip8_i->D -= 1;
    } 
    // TODO sound
    if (chip8_i->S > 0) {
      chip8_i->S -= 1;
    }

    // maintain 60 Hz
    cycle_end = SDL_GetPerformanceCounter();
    elapsed = (double)((cycle_end - cycle_start)/1000) / SDL_GetPerformanceFrequency();
    delay = 17 > elapsed ? 17 - elapsed : 0; 
    SDL_Delay(delay);
  }
}

// Jump to machine instruction at 0xNNN
void CHIP8_I_0NNN(CHIP8_t *chip8_i) {
  chip8_i->PC = chip8_i->instruction.NNN;
}

// CLS clear screen
void CHIP8_I_00E0(CHIP8_t *chip8_i) {
  memset(chip8_i->display, 0, sizeof(chip8_i->display));
}

// RET from last call
void CHIP8_I_00EE(CHIP8_t *chip8_i) {
  if (chip8_i->SP < 1) {
    printf("ERROR: stack overflow");
  }
  chip8_i->SP -= 1;
  chip8_i->PC = chip8_i->stack[chip8_i->SP];
}

// JUMP NNN - jump to address 0x0NNN
void CHIP8_I_1NNN(CHIP8_t *chip8_i) {
  chip8_i->PC = chip8_i->instruction.NNN;
}

// CALL NNN - call subroutine at address 0x0NNN
void CHIP8_I_2NNN(CHIP8_t *chip8_i) {
  if (chip8_i->SP > 11) {
    printf("ERROR: Too many nested subroutine calls...segfault inbound");
  }
  chip8_i->stack[chip8_i->SP] = chip8_i->PC;
  chip8_i->SP += 1;
  chip8_i->PC = chip8_i->instruction.NNN;
}

// SE Vx, NN - compare register X with 0xNN, skip next instruction if they are equal
void CHIP8_I_3XNN(CHIP8_t *chip8_i) {
  if (chip8_i->V[chip8_i->instruction.X] == chip8_i->instruction.NN) {
    chip8_i->PC += 2;
  }
}

// SNE Vx, Vy - compare register X with 0xNN, skip next instruction if they are not equal
void CHIP8_I_4XNN(CHIP8_t *chip8_i) {
  if (chip8_i->V[chip8_i->instruction.X] != chip8_i->instruction.NN) {
    chip8_i->PC += 2;
  }
}

// SE Vx, Vy - skip next instruction if the value in register X equals the value in register Y
void CHIP8_I_5XY0(CHIP8_t *chip8_i) {
  if (chip8_i->V[chip8_i->instruction.X] == chip8_i->V[chip8_i->instruction.Y]) {
    chip8_i->PC += 2;
  }
}

// LD Vx, NN - set register X to the value NN
void CHIP8_I_6XNN(CHIP8_t *chip8_i) {
  chip8_i->V[chip8_i->instruction.X] = chip8_i->instruction.NN; 
}

// ADD Vx, NN - add NN to the value in register X and store the result in register X
void CHIP8_I_7XNN(CHIP8_t *chip8_i) {
  chip8_i->V[chip8_i->instruction.X] += chip8_i->instruction.NN; 
}

// LD Vx, Vy - store the value in register Y in register X
void CHIP8_I_8XY0(CHIP8_t *chip8_i) {
  chip8_i->V[chip8_i->instruction.X] = chip8_i->V[chip8_i->instruction.Y];  
} 

// OR Vx, Vy - Bitwise OR the values in register X and Y and store the result in register Y
void CHIP8_I_8XY1(CHIP8_t *chip8_i) {
  chip8_i->V[chip8_i->instruction.X] |= chip8_i->V[chip8_i->instruction.Y];  
}

// AND Vx, Vy - Bitwise AND the values in register X and Y and store the result in register Y
void CHIP8_I_8XY2(CHIP8_t *chip8_i) {
  chip8_i->V[chip8_i->instruction.X] &= chip8_i->V[chip8_i->instruction.Y];  
}

// XOR Vx, Vy - Bitwise XOR the values in register X and Y and store the result in register Y
void CHIP8_I_8XY3(CHIP8_t *chip8_i) {
  chip8_i->V[chip8_i->instruction.X] ^= chip8_i->V[chip8_i->instruction.Y];  
}

// ADD Vx, Vy - Add the values in register X and Y and store the result in register X and set carry in register F
void CHIP8_I_8XY4(CHIP8_t *chip8_i) {
  uint16_t sum = chip8_i->V[chip8_i->instruction.X] + chip8_i->V[chip8_i->instruction.Y]; 
  chip8_i->V[chip8_i->instruction.X] = (uint8_t)(sum); 
  chip8_i->V[0xF] = sum > 0xFF ? 0x1 : 0x0;
}

// TODO what if VX or VY = VF?
// SUB Vx, Vy - Subtract the values in register X and Y and store the result in register Y and set carry in register F
void CHIP8_I_8XY5(CHIP8_t *chip8_i) {
  chip8_i->V[0xF] = chip8_i->V[chip8_i->instruction.X] > chip8_i->V[chip8_i->instruction.Y] ? 0x1 : 0x0;
  chip8_i->V[chip8_i->instruction.X] -= chip8_i->V[chip8_i->instruction.Y];
}

// SHR Vx {, Vy} - set register F to 1 if the least significant bit of VX is 1, else set F to 0, then divide Vx by 2
void CHIP8_I_8XY6(CHIP8_t *chip8_i) {
  chip8_i->V[0xF] = chip8_i->V[chip8_i->instruction.X] & 0x1;
  chip8_i->V[chip8_i->instruction.X] >>= 1;
}

// SUBN Vx, Vy - if Vy > Vx then set VF to 1, else 0. Then store Vy - Vx in Vx
void CHIP8_I_8XY7(CHIP8_t *chip8_i) {
  chip8_i->V[0xF] = chip8_i->V[chip8_i->instruction.Y] > chip8_i->V[chip8_i->instruction.X] ? 0x1 : 0x0;
  chip8_i->V[chip8_i->instruction.X] = chip8_i->V[chip8_i->instruction.Y] - chip8_i->V[chip8_i->instruction.X];
}

// SHL if the most significant bit of Vx is 1, set VF to 1 else 0 then multiply Vx by 2
void CHIP8_I_8XYE(CHIP8_t *chip8_i) {
  // chip8_i->V[chip8_i->instruction.X] = chip8_i->V[chip8_i->instruction.Y]; -- "Modern" behaviour, fails tests
  chip8_i->V[0xF] = (chip8_i->V[chip8_i->instruction.X] & 0x80) >> 7;
  chip8_i->V[chip8_i->instruction.X] <<= 1;
}

// SNE Vx, Vy - skip next instruction if Vx is not equal to Vy
void CHIP8_I_9XY0(CHIP8_t *chip8_i) {
  if (chip8_i->V[chip8_i->instruction.X] != chip8_i->V[chip8_i->instruction.Y]) {
    chip8_i->PC += 2;
  }
}

// LD I, NNN - copy NNN to register I, the index register
void CHIP8_I_ANNN(CHIP8_t *chip8_i) {
  chip8_i->I = chip8_i->instruction.NNN;
}

// JP V0, NNN - jump to the address obtained by adding the value in register 0 to 0xNNN
void CHIP8_I_BNNN(CHIP8_t *chip8_i) {
  chip8_i->PC = chip8_i->V[0x0] + chip8_i->instruction.NNN;
}

// MODERN VERSION: https://tobiasvl.github.io/blog/write-a-chip-8-emulator/
// JP Vx, NN - jump to the address obtained by adding the value in register Vx to 0xXNN
// void CHIP8_I_BNNN(CHIP8_t *chip8_i) {
//   chip8_i->PC = chip8_i->V[chip8_i->instruction.X] + chip8_i->instruction.NNN;
// }

// RND Vx, NN -- get a random number and bitwise AND with the immediate byte 0xNN, store in Vx
void CHIP8_I_CXNN(CHIP8_t *chip8_i) {
  chip8_i->V[chip8_i->instruction.X] = ((uint8_t)rand()) & chip8_i->instruction.NN;
}

// DRW Vx, Vy, N - display the n-byte sprite starting at memory location in I at (Vx, Vy), set VF if any pixels were erased, otherwise unset VF
void CHIP8_I_DXYN(CHIP8_t *chip8_i) {
  uint8_t X = chip8_i->V[chip8_i->instruction.X] % DISPLAY_WIDTH;
  uint8_t Y = chip8_i->V[chip8_i->instruction.Y] % DISPLAY_HEIGHT;
  uint8_t N = chip8_i->instruction.N;

  uint8_t sprite_pixel, screen_pixel;
  // uint8_t before, after;
  chip8_i->V[0xF] = 0x0;

  for (int y = 0; y < N; y++) {
    // don't wrap sprite
    if ((Y+y) >= DISPLAY_HEIGHT) break;
    for (int x = 0; x < 8; x++) {
      // before = chip8_i->display[(X+x) + (Y+y)*DISPLAY_WIDTH];
      // chip8_i->display[(X+x) + (Y+y)*DISPLAY_WIDTH] ^= (chip8_i->MM[chip8_i->I+y] & x);
      // after = chip8_i->display[(X+x) + (Y+y)*DISPLAY_WIDTH];
      // if (before == 0x1 && after == 0x0) {
      //   chip8_i->V[0xF] = 0x1;
      // }
      // don't wrap sprite
      if (X+x >= DISPLAY_WIDTH) break;

      sprite_pixel = chip8_i->MM[chip8_i->I+y] & (0x80 >> x);
      screen_pixel = chip8_i->display[(X+x) + (Y+y)*DISPLAY_WIDTH];
      // only XOR display pixels where the sprite pixel is on
      if (sprite_pixel) {
        if (screen_pixel) {
          chip8_i->V[0xF] = 0x1;
          screen_pixel = 0x0;
        } else {
          screen_pixel = 0x1;
        }
        // screen_pixel ^= sprite_pixel;
      }
      chip8_i->display[(X+x) + (Y+y)*DISPLAY_WIDTH] = screen_pixel;
    }
  }
}

// // SKP Vx - skip next instruction if Vx is pressed
void CHIP8_I_EX9E(CHIP8_t *chip8_i) {
  if (chip8_i->keypad[chip8_i->V[chip8_i->instruction.X]]) {
    chip8_i->PC += 2;
  } 
}

// // SKNP Vx - skip next instruction if Vx is not pressed
void CHIP8_I_EXA1(CHIP8_t *chip8_i) {
 if (!chip8_i->keypad[chip8_i->V[chip8_i->instruction.X]]) {
    chip8_i->PC += 2;
  } 
}

// LD Vx, DT - set Vx equal to the delay timer's value
void CHIP8_I_FX07(CHIP8_t *chip8_i) {
  chip8_i->V[chip8_i->instruction.X] = chip8_i->D;
}

// // LD Vx, K - block until a key is pressed, then store the key code in Vx
void CHIP8_I_FX0A(CHIP8_t *chip8_i) {
  // pre-decrement so that it returns to this instruction until a key is pressed
  chip8_i->PC -= 2;
  for (uint8_t i = 0x0; i <= 0xF; i++){
    // if a key is pressed, store its index in Vx and then continue program execution (don't return to this instruction)
    if (chip8_i->keypad[i]) {
      chip8_i->V[chip8_i->instruction.X] = chip8_i->keypad[i];
      chip8_i->PC += 2;
      break;
    }
  }
}

// LD DT, Vx - set the delay timer equal to the value of Vx
void CHIP8_I_FX15(CHIP8_t *chip8_i) {
  chip8_i->D = chip8_i->V[chip8_i->instruction.X];
}

// LD ST, Vx - set sound timer equal to the value of Vx
void CHIP8_I_FX18(CHIP8_t *chip8_i) {
  chip8_i->S = chip8_i->V[chip8_i->instruction.X];
}

// ADD I, Vx - add Vx to I and store the result in I
void CHIP8_I_FX1E(CHIP8_t *chip8_i) {
  chip8_i->I += chip8_i->V[chip8_i->instruction.X];
}

// LD F, Vx - set I equal to the location of the sprite for digit in Vx
void CHIP8_I_FX29(CHIP8_t *chip8_i) {
  // chip8_i->I = chip8_i->MM[chip8_i->V[chip8_i->instruction.X]]; // since we loaded the font into main memory starting at 0x0
  // TODO load memory at the canonical location
  chip8_i->I = chip8_i->V[chip8_i->instruction.X]*5; // font was loaded starting at 0x0, each character is 5 bytes
}

// LD B, Vx - store BCD representation of Vx in memory locations I...I+2
void CHIP8_I_FX33(CHIP8_t *chip8_i) {
  // get base ten digits
  // hundred's digit
  chip8_i->MM[chip8_i->I] = chip8_i->V[chip8_i->instruction.X] / 100;
  // ten's digit
  chip8_i->MM[chip8_i->I+1] = (chip8_i->V[chip8_i->instruction.X] % 100) / 10;
  // one's digit
  chip8_i->MM[chip8_i->I+2] = chip8_i->V[chip8_i->instruction.X] % 10; 
}

// LD [I], Vx - store V0 up to Vx in memory starting from [I] (the location stored in register I)
void CHIP8_I_FX55(CHIP8_t *chip8_i) {
  for (int i = 0; i <= chip8_i->instruction.X; i++) {
    chip8_i->MM[chip8_i->I + i] = chip8_i->V[i];
  }
}

// LD Vx, [I] - read memory from [I] to [I] + Vx into registers V0...Vx
void CHIP8_I_FX65(CHIP8_t *chip8_i) {
  for (int i = 0; i <= chip8_i->instruction.X; i++) {
    chip8_i->V[i] = chip8_i->MM[chip8_i->I + i];
  }
}

// Emulate an instruction
void CHIP8_emulate_instruction(CHIP8_t *chip8_i) {
  // left shift fills zeroes then OR with next byte in chip8 big endian to convert to x86 little endian
  // fetch
  chip8_i->instruction.opcode = (chip8_i->MM[chip8_i->PC] << 8) | chip8_i->MM[chip8_i->PC+1];
  chip8_i->PC += 2; // increment PC
  // decode
  chip8_i->instruction.NNN = chip8_i->instruction.opcode & 0xFFF;
  chip8_i->instruction.NN = chip8_i->instruction.opcode & 0xFF;
  chip8_i->instruction.N = chip8_i->instruction.opcode & 0xF;
  chip8_i->instruction.X = (chip8_i->instruction.opcode & 0x0F00) >> 8;
  chip8_i->instruction.Y = (chip8_i->instruction.opcode & 0x00F0) >> 4;

  #ifdef DEBUG
    printf("Executing instruction at 0x%04X with opcode 0x%04X\n", chip8_i->PC - 2, chip8_i->instruction.opcode);
  #endif
  // execute (technically I think selecting the instruction is still decode, but I can't think of a bettery way to separate them atm)
  // TODO switch to function pointers...subtables for different command 'categories'?
  switch ((chip8_i->instruction.opcode & 0xF000) >> 12) {
    case 0x0:
      switch(chip8_i->instruction.NNN) {
        case 0x0E0:
          CHIP8_I_00E0(chip8_i);
          break;
        case 0x0EE:
          CHIP8_I_00EE(chip8_i);
          break;
        default:
          CHIP8_I_0NNN(chip8_i);
      }
      break;
    case 0x1:
      CHIP8_I_1NNN(chip8_i);
      break;
    case 0x2:
      CHIP8_I_2NNN(chip8_i);
      break;
    case 0x3:
      CHIP8_I_3XNN(chip8_i);
      break;
    case 0x4:
      CHIP8_I_4XNN(chip8_i);
      break;
    case 0x5:
      if (chip8_i->instruction.N != 0x0) {
        break;
      }
      CHIP8_I_5XY0(chip8_i);
      break;
    case 0x6:
      CHIP8_I_6XNN(chip8_i);
      break;
    case 0x7:
      CHIP8_I_7XNN(chip8_i);
      break;
    case 0x8:
      switch(chip8_i->instruction.N) {
        case 0x0:
          CHIP8_I_8XY0(chip8_i);
          break;
        case 0x1:
          CHIP8_I_8XY1(chip8_i);
          break;
        case 0x2:
          CHIP8_I_8XY2(chip8_i);
          break;
        case 0x3:
          CHIP8_I_8XY3(chip8_i);
          break;
        case 0x4:
          CHIP8_I_8XY4(chip8_i);
          break;
        case 0x5:
          CHIP8_I_8XY5(chip8_i);
          break;
        case 0x6:
          CHIP8_I_8XY6(chip8_i);
          break;
        case 0x7:
          CHIP8_I_8XY7(chip8_i);
          break;
        case 0xE:
          CHIP8_I_8XYE(chip8_i);
          break;
        default:
          printf("\t^Invalid instruction or WIP\n");
      }
      break;
    case 0x9:
      CHIP8_I_9XY0(chip8_i);
      break;
    case 0xA:
      CHIP8_I_ANNN(chip8_i);
      break;
    case 0xB:
      CHIP8_I_BNNN(chip8_i);
      break;
    case 0xC:
      CHIP8_I_CXNN(chip8_i);
      break;
    case 0xD:
      CHIP8_I_DXYN(chip8_i);
      break;
    case 0xE:
      switch(chip8_i->instruction.NN) {
        case 0x9E:
          CHIP8_I_EX9E(chip8_i);
          break;
        case 0xA1:
          CHIP8_I_EXA1(chip8_i);
          break;
          default: 
            printf("\t^Invalid instruction or WIP\n");    
        }
      break;
    case 0xF:
      switch(chip8_i->instruction.NN) {
        case 0x07:
          CHIP8_I_FX07(chip8_i);
          break;
        case 0x0A:
          CHIP8_I_FX0A(chip8_i);
          break;
        case 0x15:
          CHIP8_I_FX15(chip8_i);
          break;
        case 0x18:
          CHIP8_I_FX18(chip8_i);
          break;
        case 0x1E:
          CHIP8_I_FX1E(chip8_i);
          break;
        case 0x29:
          CHIP8_I_FX29(chip8_i);
          break;
        case 0x33:
          CHIP8_I_FX33(chip8_i);
          break;
        case 0x55:
          CHIP8_I_FX55(chip8_i);
          break;
        case 0x65:
          CHIP8_I_FX65(chip8_i);
          break;
        default: 
          printf("\t^Invalid instruction or WIP\n");
      }
      break; 
    default:
      printf("\t^Invalid instruction or WIP\n");
  }
}

//
int main(int argc, char **argv) {
  // seed PRNG
  srand(time(NULL));
  // parse command line args
  char *rom_name        = argc > 1 ? argv[1] : "";
  if (!strlen(rom_name)) {
    printf("Error: no CHIP8 ROM specified. Exiting...\n");
    return -1;
  }
  uint8_t scale_factor  = argc > 2 ? (uint8_t)strtol(argv[2], NULL, 0)  : 0;
  uint32_t clock_rate   = argc > 3 ? (uint32_t)strtol(argv[3], NULL, 0) : 0;
  uint32_t bg_color     = argc > 4 ? (uint32_t)strtol(argv[4], NULL, 0) : 0x000000;
  uint32_t fg_color     = argc > 5 ? (uint32_t)strtol(argv[5], NULL, 0) : 0xFFFFFF; // default white on black

  // init SDL2
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    SDL_Log("Unable to initialize SDL: %s\n", SDL_GetError());
    return 1;
  }

  CHIP8_t *chip8_i = CHIP8_create(scale_factor, clock_rate, bg_color, fg_color);
  if (chip8_i == NULL) {
    return -1;
  }

  // TODO switch on error codes to give more informative error messaging
  int ret;
  ret = CHIP8_init(chip8_i, rom_name);
  if (ret) {
    SDL_Log("Could not start CHIP8 emulator\n");
    // return -1;
  }
  
  CHIP8_start(chip8_i);

  // this stops an SDL segfault if program exits very quickly e.g. with no input
  // SDL_Delay(1000);
  CHIP8_destroy(chip8_i);
  SDL_Quit();

  printf("TESTING ON WSL2\n");

  return 0;
}