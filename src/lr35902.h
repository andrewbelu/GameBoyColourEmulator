#ifndef __LR35902_H
#define __LR35902_H

#include <stdint.h>

void lr35902_run(const uint8_t * const rom, const size_t rom_sz);

/** NOT GOING TO USE THESE FOR NOW
// here are the Sharp LR35902 opcodes
#define NOP         0x00

// Load
#define LD_A_D8     0x3E
#define LD_A16_A    0xEA
#define LDH_A8_A    0xE0
#define LDH_A_A8    0xF0

// Jump
#define JP_A16      0xC3
#define JR_NZ       0x20
#define JR_Z        0x28
#define JR_R8       0x18

// Compare
#define CP_D8       0xFE

// Logic
#define XOR_A       0xAF

// Interrupt
#define DI          0xF3
#define EI          0xFB
NOT GOING TO USE THESE FOR NOW **/

#endif