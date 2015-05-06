#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "lr35902.h"

// assume the ROM has be read into the memory
extern unsigned int pokemon_gold_gbc_len;
extern unsigned char pokemon_gold_gbc[];

bool is_little_endian()
{
    static uint32_t n = 0xDEADBEEF;
    return 0xEF == *(uint8_t*)&n;
}

int main()
{
    unsigned char *rom = (unsigned char*)pokemon_gold_gbc;
    long int rom_sz = pokemon_gold_gbc_len;
    int i;
    
    if (!is_little_endian())
    {
        puts("Error: Non Little-Endian machine detected!");
        return -1;
    }

    lr35902_run(rom, rom_sz);

    // cpu halted
    puts("CPU Halted!\n");
    return 0;
}