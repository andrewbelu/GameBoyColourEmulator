#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "memmap.h"
#include "lr35902.h"

// temp registers
static uint8_t d8, a8;
static int8_t r8;
static uint16_t d16, a16;

/** General Registers **/
//            15 .. 8                7 .. 0 
static uint8_t reg_a;
static uint8_t reg_b; static uint8_t reg_c;
static uint8_t reg_d; static uint8_t reg_e;
static uint8_t reg_h; static uint8_t reg_l;

/** Flags **/
static uint8_t flg_z;   // Zero
static uint8_t flg_n;   // Subtract
static uint8_t flg_h;   // Half Carry
static uint8_t flg_c;   // Carry
static uint8_t flg_i;   // Interrupt Master Enable (1 == interrupt enabled) 

/** Stack Pointer and Program Counter **/
static uint16_t reg_sp, reg_pc;

/** Opcode of the current instruction **/
static uint8_t cur_opcode;
static uint8_t cur_opcodeCB;
static uint8_t cur_funcCB;  // this is decoded by opcodeCB >> 3

/*
    d8  means immediate 8 bit data
    d16 means immediate 16 bit data
    a8  means 8 bit unsigned address (0xFF00 + a8)
    a16 means 16 bit unsigned address
    r8  means 8 bit signed address (which are added to program counter)
*/

#define INC_PC() do { reg_pc += instlen[cur_opcode]; } while (0)

#define MAKE16(h, l) ((h << 8) | l)
#define MAKEBC() (MAKE16(reg_b, reg_c))
#define MAKEDE() (MAKE16(reg_d, reg_e))
#define MAKEHL() (MAKE16(reg_h, reg_l))
#define P_VALHL() (mem_mapper(MAKEHL()))

// Little-Endian
#define D16(addr) (MAKE16(*mem_mapper(addr+2), *mem_mapper(addr+1)))    // LSB first
#define D8(addr) (*mem_mapper(addr+1))
#define A16(addr) (D16(addr))
#define A8(addr) ((uint16_t)(0xFF00 | D8(addr)))
#define R8(addr) ((int8_t)D8(addr))

#define PUSH(h, l) do { *mem_mapper(--reg_sp) = h; *mem_mapper(--reg_sp) = l; } while (0)
#define POP(h, l)  do { l = *mem_mapper(reg_sp++); h = *mem_mapper(reg_sp++); } while (0)
#define PUSHBC() PUSH(reg_b, reg_c)
#define PUSHDE() PUSH(reg_d, reg_e)
#define PUSHHL() PUSH(reg_h, reg_l)
#define POPBC() POP(reg_b, reg_c)
#define POPDE() POP(reg_d, reg_e)
#define POPHL() POP(reg_h, reg_l)

#define PUSHPC() do { reg_sp -= 2; *(uint16_t*)mem_mapper(reg_sp) = reg_pc + instlen[cur_opcode]; } while (0)
#define POPPC()  do { reg_pc = *(uint16_t*)mem_mapper(reg_sp); reg_sp += 2; } while (0)

#define PUSHAF() PUSH(reg_a, ((flg_z << 7) | (flg_n << 6) | (flg_h << 5) | (flg_c << 4)))
#define POPAF() do {                          \
                        uint8_t h, l;         \
                        POP(h, l);            \
                        reg_a = h;            \
                        flg_z = (l & 0x80);   \
                        flg_n = (l & 0x40);   \
                        flg_h = (l & 0x20);   \
                        flg_c = (l & 0x10);   \
                    } while (0)      

#define IF_NOT_ROM(addr) if (addr < 0x8000)

// length of each instruction in bytes
const uint8_t instlen[256] =
{
// x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 xA xB xC xD xE xF 
    1, 3, 1, 1, 1, 1, 2, 1, 3, 1, 1, 1, 1, 1, 2, 1, // 0x
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1, // 1x
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1, // 2x
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1, // 3x
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 4x
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 5x
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 6x
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 7x
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 8x
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 9x
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // Ax
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // Bx
    1, 1, 3, 3, 3, 1, 2, 1, 1, 1, 3, 1, 3, 3, 2, 1, // Cx
    1, 1, 3, 1, 3, 1, 2, 1, 1, 1, 3, 1, 3, 1, 2, 1, // Dx
    2, 1, 2, 1, 1, 1, 2, 1, 2, 1, 3, 1, 1, 1, 2, 1, // Ex
    2, 1, 2, 1, 1, 1, 2, 1, 2, 1, 3, 1, 1, 1, 2, 1, // Fx
};

uint8_t * const regtableCB[8] = 
{
    &reg_b, &reg_c, &reg_d, &reg_e, &reg_h, &reg_l, NULL, &reg_a, 
};

inline void rlc(uint8_t *reg);
inline void rrc(uint8_t *reg);
inline void rl(uint8_t *reg);
inline void rr(uint8_t *reg);
inline void sla(uint8_t *reg);
inline void sra(uint8_t *reg);
inline void swap(uint8_t *reg);
inline void srl(uint8_t *reg);
inline void bit(uint8_t *reg);
inline void res(uint8_t *reg);
inline void set(uint8_t *reg);

void (* const functableCB[32])(uint8_t *) =
{
//  x0    x8
    rlc,  rrc,  // 0x
    rl,   rr,   // 1x
    sla,  sra,  // 2x
    swap, srl,  // 3x
    bit,  bit,  // 4x
    bit,  bit,  // 5x
    bit,  bit,  // 6x
    bit,  bit,  // 7x
    res,  res,  // 8x
    res,  res,  // 9x
    res,  res,  // Ax
    res,  res,  // Bx
    set,  set,  // Cx
    set,  set,  // Dx
    set,  set,  // Ex
    set,  set,  // Fx
};


// RLC
inline void rlc(uint8_t *reg)
{
    flg_c = *reg >> 7;
    *reg = (*reg << 1) | flg_c; 
    flg_z = (0 == *reg);
    flg_n = 0;
    flg_h = 0;
}

// RRC
inline void rrc(uint8_t *reg)
{
    flg_c = *reg & 0x01;
    *reg = (*reg >> 1) | (flg_c << 7); 
    flg_z = (0 == *reg);
    flg_n = 0;
    flg_h = 0;
}

// RL
inline void rl(uint8_t *reg)
{
    uint8_t old_flg_c = flg_c;
    flg_c = *reg >> 7;
    *reg = (*reg << 1) | old_flg_c;
    flg_z = (0 == *reg);
    flg_n = 0;
    flg_h = 0;
}

// RR
inline void rr(uint8_t *reg)
{
    uint8_t old_flg_c = flg_c;
    flg_c = *reg & 0x01;
    *reg = (*reg >> 1) | (old_flg_c << 7); 
    flg_z = (0 == *reg);
    flg_n = 0;
    flg_h = 0;
}

// SLA
inline void sla(uint8_t *reg)
{
    flg_c = *reg & 0x80;
    *reg <<= 1;
    flg_z = (0 == *reg);
    flg_n = 0;
    flg_h = 0;
}

// SRA
inline void sra(uint8_t *reg)
{
    flg_c = *reg & 0x01;
    *(int8_t*)reg >>= 1;    // signed shift
    flg_z = (0 == *reg);
    flg_n = 0;
    flg_h = 0;
}

// SWAP
inline void swap(uint8_t *reg)
{
    *reg = (*reg >> 4) | (*reg << 4);
    flg_z = (0 == *reg);
    flg_n = 0;
    flg_h = 0;
    flg_c = 0;
}

// SRL
inline void srl(uint8_t *reg)
{
    flg_c = *reg & 0x01;
    *reg >>= 1;
    flg_z = (0 == *reg);
    flg_n = 0;
    flg_h = 0;
}

// BIT
inline void bit(uint8_t *reg)
{
    flg_z = (0 == (*reg & (1 << (cur_funcCB & 0x07))));
    flg_n = 0;
    flg_h = 1;
    // flg_c = flg_c;
}

// RES
inline void res(uint8_t *reg)
{
    *reg &= ~(1 << (cur_funcCB & 0x07));
}

// SET
inline void set(uint8_t *reg)
{
    *reg |= (1 << (cur_funcCB & 0x07));
}


// LD n, n
inline void ld8(uint8_t *reg, uint8_t val)
{
    // load the reg
    *reg = val;

    // increase the PC
    INC_PC();
}

// LD nn, nn
inline void ld16(uint8_t *__reg_h, uint8_t *__reg_l, uint8_t val_h, uint8_t val_l)
{
    // load the regs (LSB first)
    *__reg_l = val_l;
    *__reg_h = val_h;

    // increase the PC
    INC_PC();
}

// LDHL SP, n
inline void ldhl_sp_n(int8_t val)
{
    // load vals from SP
    uint32_t temp32 = reg_sp;
    uint32_t temp32_h = reg_sp & 0x0FFF;

    // calculate
    temp32 += (uint16_t)val;
    temp32_h += val & 0x0FFF;

    // calc the reg and flags
    reg_sp = (uint16_t)temp32;
    flg_z = 0;
    flg_n = 0;
    flg_h = temp32_h >> 12;
    flg_c = (temp32 >> 16) ? 1 : 0;

    // increase the PC
    INC_PC();
}

// LD SP, nn
inline void ldsp(uint16_t val)
{
    // load the SP
    reg_sp = val;

    // increase the PC
    INC_PC();
}

/* LDD/LDI A, (HL) */
inline void __ld_a(uint8_t n)
{
    // get HL
    uint16_t temp16 = MAKEHL();
    
    // load (HL) into A and dec HL
    reg_a = *mem_mapper(temp16);
    temp16 += n;
    reg_l = (uint8_t)temp16;
    reg_h = temp16 >> 8;

    // increse PC
    INC_PC();
}

/* LDD/LDI (HL), A */
inline void __ld_hl(uint8_t n)
{
    // get HL
    uint16_t temp16 = MAKEHL();
    
    // load A into (HL) and dec HL
    *mem_mapper(temp16) = reg_a;
    temp16 += n;
    reg_l = (uint8_t)temp16;
    reg_h = temp16 >> 8;

    // increse PC
    INC_PC();
}

#define ldda() __ld_a(-1)
#define ldia() __ld_a(1)
#define lddhl() __ld_hl(-1)
#define ldihl() __ld_hl(1)

// ADC A, n
inline void adc(uint8_t val)
{
    uint8_t  temp8 = flg_c;
    uint16_t temp16 = flg_c;

    // calculations
    temp8  += (reg_a & 0x0F) + (val & 0x0F);
    temp16 += reg_a + val;
 
    // calc the reg and flags
    reg_a = (uint8_t)temp16;
    flg_z = (0 == reg_a);    
    flg_n = 0;
    flg_h = temp8 >> 4;
    flg_c = (temp16 >> 8) ? 1 : 0;

    // increase the PC
    INC_PC();
}

// ADD A, n
inline void add(uint8_t val)
{
    // ADD is just ADC with no carry
    flg_c = 0;
    adc(val);
}

// SBC A, n
inline void sbc(uint8_t val)
{
    uint8_t  temp8 = (reg_a & 0x0F) - flg_c;
    uint16_t temp16 = reg_a - flg_c;
    
    // calculations
    temp8  -= val & 0x0F;
    temp16 -= val;
 
    // calc the reg and flags
    reg_a = (uint8_t)temp16;
    flg_z = (0 == reg_a);    
    flg_n = 1;
    flg_h = temp8 >> 4;
    flg_c = (temp16 >> 8) ? 1 : 0;

    // increase the PC
    INC_PC();
}

// SUB A, n
inline void sub(uint8_t val)
{
    // SUB is just SBC with no carry
    flg_c = 0;
    sbc(val);
}

// AND n
inline void and(uint8_t val)
{
    // calc the reg and flags
    reg_a &= val;
    flg_z = (0 == reg_a);    
    flg_n = 0;
    flg_h = 1;
    flg_c = 0;

    // increase the PC
    INC_PC();
}

// OR n
inline void or(uint8_t val)
{
    // calc the reg and flags
    reg_a |= val;
    flg_z = (0 == reg_a);    
    flg_n = 0;
    flg_h = 0;
    flg_c = 0;

    // increase the PC
    INC_PC();
}

// XOR n
inline void xor(uint8_t val)
{
    // calc the reg and flags
    reg_a ^= val;
    flg_z = (0 == reg_a);
    flg_n = 0;
    flg_h = 0;
    flg_c = 0;

    // increase the PC
    INC_PC();
}

// CP n
inline void cp(uint8_t val)
{
    // calc the flags
    flg_z = (reg_a == val);    
    flg_n = 1;
    flg_h = (reg_a & 0x0F) < (val & 0x0F);
    flg_c = (reg_a < val) ? 1 : 0;

    // increase the PC
    INC_PC();
}

// INC n
inline void inc8(uint8_t *reg)
{
    // calc the reg and flags
    (*reg)++;
    flg_z = (0 == *reg);    
    flg_n = 0;
    flg_h = (0 == (*reg & 0xF0));
    // flg_c = flg_c;

    // increase the PC
    INC_PC();
}

// DEC n
inline void dec8(uint8_t *reg)
{
    // calc the reg and flags
    (*reg)--;
    flg_z = (0 == *reg);    
    flg_n = 1;
    flg_h = (0x0F == (*reg & 0xF0));
    // flg_c = flg_c;

    // increase the PC
    INC_PC();
}

// ADD HL, nn
inline void addhl(uint16_t val)
{
    // load vals from H & L
    uint32_t temp32 = MAKEHL();
    uint32_t temp32_h = temp32 & 0x0FFF;

    // calculate
    temp32 += val;
    temp32_h += val & 0x0FFF;

    // calc the reg and flags
    reg_l = (uint8_t)temp32;
    reg_h = (uint8_t)(temp32 >> 8);
    // flg_z = flg_z
    flg_n = 0;
    flg_h = temp32_h >> 12;
    flg_c = (temp32 >> 16) ? 1 : 0;

    // increase the PC
    INC_PC();
}

// ADD SP, n
inline void addsp(int8_t val)
{
    // load vals from SP
    uint32_t temp32 = reg_sp;
    uint32_t temp32_h = reg_sp & 0x0FFF;

    // calculate
    temp32 += (uint16_t)val;
    temp32_h += val & 0x0FFF;

    // calc the reg and flags
    reg_sp = (uint16_t)temp32;
    flg_z = 0;
    flg_n = 0;
    flg_h = temp32_h >> 12;
    flg_c = (temp32 >> 16) ? 1 : 0;

    // increase the PC
    INC_PC();
}

// INC nn
inline void __inc16(uint8_t *__reg_h, uint8_t *__reg_l, int8_t n)
{
    // load and inc the values
    uint16_t temp16 = MAKE16(*__reg_h, *__reg_l) + n;

    // store the regs
    *__reg_h = (uint8_t)(temp16 >> 8);
    *__reg_l = (uint8_t)temp16;

    // increase the PC
    INC_PC();
}

#define inc16(h, l) __inc16(h, l, 1)
#define dec16(h, l) __inc16(h, l, -1)

// INC SP
inline void __incsp(int8_t n)
{
    reg_sp += n;
    INC_PC();
}

#define incsp() __incsp(1)
#define decsp() __incsp(-1)

// CPL
inline void cpl()
{
    reg_a = ~reg_a;
    // flg_z = flg_z;
    flg_n = 1;
    flg_h = 1;
    // flg_c = flg_c;
    INC_PC();
}

// CCF
inline void ccf()
{
    // flg_z = flg_z;
    flg_n = 0;
    flg_h = 0;
    flg_c = !flg_c;
    INC_PC();
}

// SCF
inline void scf()
{
    // flg_z = flg_z;
    flg_n = 0;
    flg_h = 0;
    flg_c = 1;
    INC_PC();
}

// JP cc, nn
inline void jp(flg)
{
    if (flg) reg_pc = A16(reg_pc);
    else     INC_PC();
}

// JR cc, n
inline void jr(flg)
{
    if (flg) reg_pc += R8(reg_pc);
    INC_PC();
}

// CALL [cc], nn
inline void call(uint8_t flg)
{
    if (flg)
    {
        PUSHPC();
        reg_pc = A16(reg_pc);
    }
    else
    {
        // increse PC
        INC_PC();
    }
}

// RST n
inline void rst(uint8_t addr)
{
    PUSHPC();
    reg_pc = addr;
}

// RET
inline void ret(uint8_t flg)
{
    if (flg) POPPC();
    else     INC_PC();
}

// RETI
inline void reti()
{
    POPPC();
    flg_i = 1;
}

// DI/EI
inline void di() { flg_i = 0; INC_PC(); }
inline void ei() { flg_i = 1; INC_PC(); }

// DAA
inline void daa()
{
    // load A
    uint16_t temp16 = reg_a;

    // this is based on a code that's based on another code posted by Blarrg
    if (!flg_n)
    {
        if (flg_h || ((temp16 & 0x0F) > 0x09))
            temp16 += 0x06;

        if (flg_c || (temp16 > 0x9F))
            temp16 += 0x60;
    }
    else
    {
        if (flg_h)
            temp16 -= 0x06;

        if (flg_c)
            temp16 -= 0x60;
    }

    // store A
    reg_a = (uint8_t)temp16;

    // calc flags
    flg_z = (0 == reg_a);
    // flg_n = flg_n;
    flg_h = 0;
    flg_c = (temp16 >> 8) ? 1 : 0;

    // increase the PC
    INC_PC();
}

// NOP
inline void nop()
{
    reg_pc++;
}

inline void lr35902_decodeCB()
{
    // get opcodeCB and reg idx
    cur_opcodeCB = D8(cur_opcode);
    cur_funcCB = cur_opcodeCB >> 3;
    uint8_t reg_idx = cur_opcodeCB & 0x06;

    // check if we need to fetch (HL)
    if (0x6 == reg_idx)
        (*functableCB[cur_funcCB])(mem_mapper(MAKEHL()));
    else
        (*functableCB[cur_funcCB])(regtableCB[reg_idx]);

    // increase PC
    reg_pc += 2;
}

inline void lr35902_decode()
{
    // temp vars
    //uint8_t temp8;
    //uint16_t temp16;

	// check that the value of Carry Flag (reg_c) is valid (ie. 0 or 1)
	assert((reg_c == 0) || (reg_c == 1));
	
    // big switch table to decode instructions (hope this turns into a jump table)
    switch (cur_opcode = *mem_mapper(reg_pc))
    {
/****************  8-Bit LOAD  ****************/

        /* LD nn, n */
        case 0x06: ld8(&reg_b, D8(reg_pc)); break;
        case 0x0E: ld8(&reg_c, D8(reg_pc)); break;
        case 0x16: ld8(&reg_d, D8(reg_pc)); break;
        case 0x1E: ld8(&reg_e, D8(reg_pc)); break;
        case 0x26: ld8(&reg_h, D8(reg_pc)); break;
        case 0x2E: ld8(&reg_l, D8(reg_pc)); break;

        /* LD n, A */
        case 0x02: ld8(mem_mapper(MAKEBC()), reg_a); break;
        case 0x12: ld8(mem_mapper(MAKEDE()), reg_a); break;
        case 0x77: ld8(mem_mapper(MAKEHL()), reg_a); break;
        case 0xEA: ld8(mem_mapper(D16(reg_pc)), reg_a); break;

        /* LD A, n */
        case 0x7F: ld8(&reg_a, reg_a); break;
        case 0x78: ld8(&reg_a, reg_b); break;
        case 0x79: ld8(&reg_a, reg_c); break;
        case 0x7A: ld8(&reg_a, reg_d); break;
        case 0x7B: ld8(&reg_a, reg_e); break;
        case 0x7C: ld8(&reg_a, reg_h); break;
        case 0x7D: ld8(&reg_a, reg_l); break;
        case 0x0A: ld8(&reg_a, *mem_mapper(MAKEBC())); break;
        case 0x1A: ld8(&reg_a, *mem_mapper(MAKEDE())); break;
        case 0xFA: ld8(&reg_a, *mem_mapper(MAKEHL())); break;
        case 0x7E: ld8(&reg_a, *mem_mapper(D16(reg_pc))); break;
        case 0x3E: ld8(&reg_a, D8(reg_pc)); break;

        /* LD B, n */
        case 0x47: ld8(&reg_b, reg_a); break;
        case 0x40: ld8(&reg_b, reg_b); break;
        case 0x41: ld8(&reg_b, reg_c); break;
        case 0x42: ld8(&reg_b, reg_d); break;
        case 0x43: ld8(&reg_b, reg_e); break;
        case 0x44: ld8(&reg_b, reg_h); break;
        case 0x45: ld8(&reg_b, reg_l); break;
        case 0x46: ld8(&reg_b, *P_VALHL()); break;

        /* LD C, n */
        case 0x4F: ld8(&reg_c, reg_a); break;
        case 0x48: ld8(&reg_c, reg_b); break;
        case 0x49: ld8(&reg_c, reg_c); break;
        case 0x4A: ld8(&reg_c, reg_d); break;
        case 0x4B: ld8(&reg_c, reg_e); break;
        case 0x4C: ld8(&reg_c, reg_h); break;
        case 0x4D: ld8(&reg_c, reg_l); break;
        case 0x4E: ld8(&reg_c, *P_VALHL());break;

        /* LD D, n */
        case 0x57: ld8(&reg_d, reg_a); break;
        case 0x50: ld8(&reg_d, reg_b); break;
        case 0x51: ld8(&reg_d, reg_c); break;
        case 0x52: ld8(&reg_d, reg_d); break;
        case 0x53: ld8(&reg_d, reg_e); break;
        case 0x54: ld8(&reg_d, reg_h); break;
        case 0x55: ld8(&reg_d, reg_l); break;
        case 0x56: ld8(&reg_d, *P_VALHL()); break;
        
        /* LD E, n */
        case 0x5F: ld8(&reg_e, reg_a); break;
        case 0x58: ld8(&reg_e, reg_b); break;
        case 0x59: ld8(&reg_e, reg_c); break;
        case 0x5A: ld8(&reg_e, reg_d); break;
        case 0x5B: ld8(&reg_e, reg_e); break;
        case 0x5C: ld8(&reg_e, reg_h); break;
        case 0x5D: ld8(&reg_e, reg_l); break;
        case 0x5E: ld8(&reg_e, *P_VALHL()); break;

        /* LD H, n */
        case 0x67: ld8(&reg_h, reg_a); break;
        case 0x60: ld8(&reg_h, reg_b); break;
        case 0x61: ld8(&reg_h, reg_c); break;
        case 0x62: ld8(&reg_h, reg_d); break;
        case 0x63: ld8(&reg_h, reg_e); break;
        case 0x64: ld8(&reg_h, reg_h); break;
        case 0x65: ld8(&reg_h, reg_l); break;
        case 0x66: ld8(&reg_h, *P_VALHL()); break;

        /* LD L, n */
        case 0x6F: ld8(&reg_l, reg_a); break;
        case 0x68: ld8(&reg_l, reg_b); break;
        case 0x69: ld8(&reg_l, reg_c); break;
        case 0x6A: ld8(&reg_l, reg_d); break;
        case 0x6B: ld8(&reg_l, reg_e); break;
        case 0x6C: ld8(&reg_l, reg_h); break;
        case 0x6D: ld8(&reg_l, reg_l); break;
        case 0x6E: ld8(&reg_l, *P_VALHL()); break;

        /* LD (HL), n */
        case 0x70: ld8(P_VALHL(), reg_b); break;
        case 0x71: ld8(P_VALHL(), reg_c); break;
        case 0x72: ld8(P_VALHL(), reg_d); break;
        case 0x73: ld8(P_VALHL(), reg_e); break;
        case 0x74: ld8(P_VALHL(), reg_h); break;
        case 0x75: ld8(P_VALHL(), reg_l); break;
        case 0x36: ld8(P_VALHL(), D8(reg_pc)); break;

        /* LDD/LDI */
        case 0x22: ldihl(); break;
        case 0x32: lddhl(); break;
        case 0x2A: ldia(); break;
        case 0x3A: ldda(); break;
        case 0xE2: ld8(mem_mapper(0xFF00 | reg_c), reg_a); break;
        case 0xF2: ld8(&reg_a, *mem_mapper(0xFF00 | reg_c)); break;

        /* LDH */
        case 0xE0: ld8(mem_mapper(A8(reg_pc)), reg_a); break;
        case 0xF0: ld8(&reg_a, *mem_mapper(A8(reg_pc))); break;

/****************  16-Bit LOAD  ****************/

        /* LD nn, nn */
        case 0x01: ld16(&reg_b, &reg_c, D8(reg_pc+2), D8(reg_pc+1)); break;
        case 0x11: ld16(&reg_d, &reg_e, D8(reg_pc+2), D8(reg_pc+1)); break;
        case 0x21: ld16(&reg_h, &reg_l, D8(reg_pc+2), D8(reg_pc+1)); break;
        case 0x31: ldsp(D16(reg_pc)); break;

        // LD SP, HL
        case 0xF9: ldsp(MAKEHL()); break;

        // LDHL SP, n
        case 0xF8: ldhl_sp_n(R8(reg_pc)); break;


/****************  ALU  ****************/

        /* ADD */
        case 0x87: add(reg_a); break;
        case 0x80: add(reg_b); break;
        case 0x81: add(reg_c); break;
        case 0x82: add(reg_d); break;
        case 0x83: add(reg_e); break;
        case 0x84: add(reg_h); break;
        case 0x85: add(reg_l); break;
        case 0x86: add(*P_VALHL()); break;
        case 0xC6: add(D8(reg_pc)); break;

        /* ADC */
        case 0x8F: adc(reg_a); break;
        case 0x88: adc(reg_b); break;
        case 0x89: adc(reg_c); break;
        case 0x8A: adc(reg_d); break;
        case 0x8B: adc(reg_e); break;
        case 0x8C: adc(reg_h); break;
        case 0x8D: adc(reg_l); break;
        case 0x8E: adc(*P_VALHL()); break;
        case 0xCE: adc(D8(reg_pc)); break;

        /* SUB */
        case 0x97: sub(reg_a); break;
        case 0x90: sub(reg_b); break;
        case 0x91: sub(reg_c); break;
        case 0x92: sub(reg_d); break;
        case 0x93: sub(reg_e); break;
        case 0x94: sub(reg_h); break;
        case 0x95: sub(reg_l); break;
        case 0x96: sub(*P_VALHL()); break;
        case 0xD6: sub(D8(reg_pc)); break;

        /* SBC */
        case 0x9F: sbc(reg_a); break;        
        case 0x98: sbc(reg_b); break;
        case 0x99: sbc(reg_c); break;
        case 0x9A: sbc(reg_d); break;
        case 0x9B: sbc(reg_e); break;
        case 0x9C: sbc(reg_h); break;
        case 0x9D: sbc(reg_l); break;
        case 0x9E: sbc(*P_VALHL()); break;
        case 0xDE: sbc(D8(reg_pc)); break;

        /* AND */
        case 0xA7: and(reg_a); break;
        case 0xA0: and(reg_b); break;
        case 0xA1: and(reg_c); break;
        case 0xA2: and(reg_d); break;
        case 0xA3: and(reg_e); break;
        case 0xA4: and(reg_h); break;
        case 0xA5: and(reg_l); break;
        case 0xA6: and(*P_VALHL()); break;
        case 0xE6: and(D8(reg_pc)); break;

        /* OR */
        case 0xB7: or(reg_a); break;
        case 0xB0: or(reg_b); break;
        case 0xB1: or(reg_c); break;
        case 0xB2: or(reg_d); break;
        case 0xB3: or(reg_e); break;
        case 0xB4: or(reg_h); break;
        case 0xB5: or(reg_l); break;
        case 0xB6: or(*P_VALHL()); break;
        case 0xF6: or(D8(reg_pc)); break;

        /* XOR */
        case 0xAF: xor(reg_a); break;
        case 0xA8: xor(reg_b); break;
        case 0xA9: xor(reg_c); break;
        case 0xAA: xor(reg_d); break;
        case 0xAB: xor(reg_e); break;
        case 0xAC: xor(reg_h); break;
        case 0xAD: xor(reg_l); break;
        case 0xAE: xor(*P_VALHL()); break;
        case 0xEE: xor(D8(reg_pc)); break;

        /* CP */
        case 0xBF: cp(reg_a); break;
        case 0xB8: cp(reg_b); break;
        case 0xB9: cp(reg_c); break;
        case 0xBA: cp(reg_d); break;
        case 0xBB: cp(reg_e); break;
        case 0xBC: cp(reg_h); break;
        case 0xBD: cp(reg_l); break;
        case 0xBE: cp(*P_VALHL()); break;
        case 0xFE: cp(D8(reg_pc)); break;

        /* INC */
        case 0x3C: inc8(&reg_a); break;
        case 0x04: inc8(&reg_b); break;
        case 0x0C: inc8(&reg_c); break;
        case 0x14: inc8(&reg_d); break;
        case 0x1C: inc8(&reg_e); break;
        case 0x24: inc8(&reg_h); break;
        case 0x2C: inc8(&reg_l); break;
        case 0x34: inc8(P_VALHL()); break;

        /* DEC */
        case 0x3D: dec8(&reg_a); break;
        case 0x05: dec8(&reg_b); break;
        case 0x0D: dec8(&reg_c); break;
        case 0x15: dec8(&reg_d); break;
        case 0x1D: dec8(&reg_e); break;
        case 0x25: dec8(&reg_h); break;
        case 0x2D: dec8(&reg_l); break;
        case 0x35: dec8(P_VALHL()); break;

        /* ADD HL/ADD SP */
        case 0x09: addhl(MAKEBC()); break;
        case 0x19: addhl(MAKEDE()); break;
        case 0x29: addhl(MAKEHL()); break;
        case 0x39: addhl(reg_sp); break;
        case 0xE8: addsp(R8(reg_pc)); break;

        /* INC nn */
        case 0x03: inc16(&reg_b, &reg_c); break;
        case 0x13: inc16(&reg_d, &reg_e); break;
        case 0x23: inc16(&reg_h, &reg_l); break;
        case 0x33: incsp(); break;

        /* DEC nn */
        case 0x0B: dec16(&reg_b, &reg_c); break;
        case 0x1B: dec16(&reg_d, &reg_e); break;
        case 0x2B: dec16(&reg_h, &reg_l); break;
        case 0x3B: decsp(); break;

        /* CB Prefix */
        case 0xCB: lr35902_decodeCB(); break;

/****************  JUMP/CALL/RET  ****************/

        /* JP cc, nn */
        case 0xC2: jp(!flg_z); break;
        case 0xCA: jp( flg_z); break;
        case 0xD2: jp(!flg_c); break;
        case 0xDA: jp( flg_c); break;

        /* JR cc, n */
        case 0x20: jr(!flg_z); break;
        case 0x28: jr( flg_z); break;
        case 0x30: jr(!flg_c); break;
        case 0x38: jr( flg_c); break;

        /* JP (misc.) */
        case 0x18: jr(1); break;
        case 0xC3: jp(1); break;
        //case 0xE9: jp(MAKEHL()); break;

        /* CALL */
        case 0xCD: call(1); break;
        case 0xC4: call(!flg_z); break;
        case 0xCC: call( flg_z); break;
        case 0xD4: call(!flg_c); break;
        case 0xDC: call( flg_c); break;

        /* RST */
        case 0xC7: rst(0x00); break;
        case 0xCF: rst(0x08); break;
        case 0xD7: rst(0x10); break;
        case 0xDF: rst(0x18); break;
        case 0xE7: rst(0x20); break;
        case 0xEF: rst(0x28); break;
        case 0xF7: rst(0x30); break;
        case 0xFF: rst(0x38); break;

        /* RET/RETI */
        case 0xC9: ret(1); break;
        case 0xC0: ret(!flg_z); break;
        case 0xC8: ret( flg_z); break;
        case 0xD0: ret(!flg_c); break;
        case 0xD8: ret( flg_c); break;
        case 0xD9: reti(); break;

/****************  MISC  ****************/
        /* NOP */
        case 0x00: nop(); break;

        /* DI/EI */
        case 0xF3: di(); break;
        case 0xFB: ei(); break;

        /* CPL/CCF/SCF */
        case 0x2F: cpl(); break;
        case 0x3F: ccf(); break;
        case 0x37: scf(); break;

        /* DAA */
        case 0x27: daa(); break;





















































        // invalid opodes
        default:
            printf("Invalid opcode 0x%X detected at PC=0x%X\n", *mem_mapper(reg_pc), reg_pc);
            // normally the cpu treats invalid opcodes as NOPs
            // but we'll just halt it for now
    }
}

void lr35902_run(const uint8_t * const r, const size_t rom_sz)
{
    // after running the bootrom, the cpu starts running the code on the rom @ 0x100
    reg_pc = 0x100;

    // fetcg-decode-execute
    for (;;)
    {
        printf("PC=0x%X\n", reg_pc);
        getchar();

        // fetch and decode
        lr35902_decode();
    }
    // cpu halted
}