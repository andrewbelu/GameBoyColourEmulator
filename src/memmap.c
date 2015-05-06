#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "memmap.h"

// TODO: assume the ROM has be read into the memory for now
extern unsigned int pokemon_gold_gbc_len;
extern unsigned char pokemon_gold_gbc[];

/** Different Types of Memories **/
//static const uint8_t *rom;            // ROM
static const uint8_t *rom = (uint8_t*)pokemon_gold_gbc;
static       uint8_t *wram;           // Working RAM
static       uint8_t  vram [8*1024];  // 8kB Video RAM
static       uint8_t  iram [8*1024];  // 8kB Internal RAM
static       uint8_t  oam  [0xA0];    // Sprite Attrib Memory (OAM)
static       uint8_t  hram [0x7F];    // (High) Internal RAM

// TODO
uint8_t tempworkram[8*1024];
uint8_t tempio[0x4C];

uint8_t *mem_mapper (uint16_t addr)
{
    // interrupt enable register
    static uint8_t ie;

    // Interrupt Enable Register
    if (addr == 0xFFFF)
    {
        return &ie;
    }
    // Internal RAM
    else if (addr >= 0xFF80)
    {
        return hram + (addr - 0xFF80);
    }
#ifdef GENERATE_UNUSED_MAPPING
    // Empty but Unusable for I/O
    else if (addr >= 0xFF4C)
    {
        puts("Warning: I/O in unused regions.\n")
    }
#endif    
    // I/O ports
    else if (addr >= 0xFF00)
    {
        // TODO
        return (uint8_t*)&tempio + (addr - 0xFF00);
    }
#ifdef GENERATE_UNUSED_MAPPING
    // Empty but Unusable for I/O
    else if (addr >= 0xFEA0)
    {
        puts("Warning: I/O in unused regions.\n")
    }
#endif
    // Sprite Attrib Memory (OAM)
    else if (addr >= 0xFE00)
    {
        return oam + (addr - 0xFE00);
    }
    // Echo of 8kB Internal RAM
    else if (addr >= 0xE000)
    {
        return iram + (addr - 0xE000);
    }
    // 8kB Internal RAM
    else if (addr >= 0xC000)
    {
        return iram + (addr - 0xC000);
    }
    // 8kB Switchable RAM bank
    else if (addr >= 0xA000)
    {
        //TODO
        return (uint8_t*)&tempworkram + (addr - 0xA000);
    }
    // 8kB Video RAM
    else if (addr >= 0x8000)
    {
        return vram + (addr - 0x8000);
    }
    // 16kB Switchable ROM bank
    else if (addr >= 0x4000)
    {
        // TODO
        return (uint8_t*)rom + addr;
    }
    // 16kB ROM Bank #0 (0x0000)
    else
    {
        return (uint8_t*)rom + addr;
    }
}