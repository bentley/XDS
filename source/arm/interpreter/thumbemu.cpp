/*  thumbemu.c -- Thumb instruction emulation.
    Copyright (C) 1996, Cygnus Software Technologies Ltd.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA. */

/* We can provide simple Thumb simulation by decoding the Thumb
instruction into its corresponding ARM instruction, and using the
existing ARM simulator.  */

#include "Kernel.h"

#include "arm/skyeye_common/skyeye_defs.h"

#ifndef MODET			/* required for the Thumb instruction support */
#if 1
#error "MODET needs to be defined for the Thumb world to work"
#else
#define MODET (1)
#endif
#endif

#include "arm/skyeye_common/armdefs.h"
#include "arm/skyeye_common/armemu.h"
#include "arm/skyeye_common/armos.h"


/* Decode a 16bit Thumb instruction.  The instruction is in the low
   16-bits of the tinstr field, with the following Thumb instruction
   held in the high 16-bits.  Passing in two Thumb instructions allows
   easier simulation of the special dual BL instruction.  */

tdstate
ARMul_ThumbDecode(
ARMul_State *state,
ARMword pc,
ARMword tinstr,
ARMword *ainstr)
{
    tdstate valid = t_decoded;	/* default assumes a valid instruction */
    ARMword next_instr;

    if (state->bigendSig) {
        next_instr = tinstr & 0xFFFF;
        tinstr >>= 16;
    }
    else {
        next_instr = tinstr >> 16;
        tinstr &= 0xFFFF;
    }

#if 1				/* debugging to catch non updates */
    * ainstr = 0xDEADC0DE;
#endif

    switch ((tinstr & 0xF800) >> 11) {
    case 0:		/* LSL */
    case 1:		/* LSR */
    case 2:		/* ASR */
        /* Format 1 */
        *ainstr = 0xE1B00000	/* base opcode */
            | ((tinstr & 0x1800) >> (11 - 5))	/* shift type */
            | ((tinstr & 0x07C0) << (7 - 6))	/* imm5 */
            | ((tinstr & 0x0038) >> 3)	/* Rs */
            | ((tinstr & 0x0007) << 12);	/* Rd */
        break;
    case 3:		/* ADD/SUB */
        /* Format 2 */
    {
        ARMword subset[4] = {
            0xE0900000,	/* ADDS Rd,Rs,Rn    */
            0xE0500000,	/* SUBS Rd,Rs,Rn    */
            0xE2900000,	/* ADDS Rd,Rs,#imm3 */
            0xE2500000	/* SUBS Rd,Rs,#imm3 */
        };
        /* It is quicker indexing into a table, than performing switch
           or conditionals: */
        *ainstr = subset[(tinstr & 0x0600) >> 9]	/* base opcode */
            | ((tinstr & 0x01C0) >> 6)	/* Rn or imm3 */
            | ((tinstr & 0x0038) << (16 - 3))	/* Rs */
            | ((tinstr & 0x0007) << (12 - 0));	/* Rd */
    }
    break;
    case 4:		/* MOV */
    case 5:		/* CMP */
    case 6:		/* ADD */
    case 7:		/* SUB */
        /* Format 3 */
    {
        ARMword subset[4] = {
            0xE3B00000,	/* MOVS Rd,#imm8    */
            0xE3500000,	/* CMP  Rd,#imm8    */
            0xE2900000,	/* ADDS Rd,Rd,#imm8 */
            0xE2500000,	/* SUBS Rd,Rd,#imm8 */
        };
        *ainstr = subset[(tinstr & 0x1800) >> 11]	/* base opcode */
            | ((tinstr & 0x00FF) >> 0)	/* imm8 */
            | ((tinstr & 0x0700) << (16 - 8))	/* Rn */
            | ((tinstr & 0x0700) << (12 - 8));	/* Rd */
    }
    break;
    case 8:		/* Arithmetic and high register transfers */
        /* TODO: Since the subsets for both Format 4 and Format 5
           instructions are made up of different ARM encodings, we could
           save the following conditional, and just have one large
           subset. */
        if ((tinstr & (1 << 10)) == 0) {
            /* Format 4 */
            enum OpcodeType { t_norm, t_shift, t_neg, t_mul };
            struct ThumbOpcode {
                ARMword opcode;
                OpcodeType otype;
            };

            ThumbOpcode subset[16] = {
                {
                    0xE0100000, t_norm
                },	/* ANDS Rd,Rd,Rs     */
                {
                    0xE0300000, t_norm
                },	/* EORS Rd,Rd,Rs     */
                {
                    0xE1B00010, t_shift
                },	/* MOVS Rd,Rd,LSL Rs */
                {
                    0xE1B00030, t_shift
                },	/* MOVS Rd,Rd,LSR Rs */
                {
                    0xE1B00050, t_shift
                },	/* MOVS Rd,Rd,ASR Rs */
                {
                    0xE0B00000, t_norm
                },	/* ADCS Rd,Rd,Rs     */
                {
                    0xE0D00000, t_norm
                },	/* SBCS Rd,Rd,Rs     */
                {
                    0xE1B00070, t_shift
                },	/* MOVS Rd,Rd,ROR Rs */
                {
                    0xE1100000, t_norm
                },	/* TST  Rd,Rs        */
                {
                    0xE2700000, t_neg
                },	/* RSBS Rd,Rs,#0     */
                {
                    0xE1500000, t_norm
                },	/* CMP  Rd,Rs        */
                {
                    0xE1700000, t_norm
                },	/* CMN  Rd,Rs        */
                {
                    0xE1900000, t_norm
                },	/* ORRS Rd,Rd,Rs     */
                {
                    0xE0100090, t_mul
                },	/* MULS Rd,Rd,Rs     */
                {
                    0xE1D00000, t_norm
                },	/* BICS Rd,Rd,Rs     */
                {
                    0xE1F00000, t_norm
                }	/* MVNS Rd,Rs        */
            };
            *ainstr = subset[(tinstr & 0x03C0) >> 6].opcode;	/* base */
            switch (subset[(tinstr & 0x03C0) >> 6].otype) {
            case t_norm:
                *ainstr |= ((tinstr & 0x0007) << 16)	/* Rn */
                    | ((tinstr & 0x0007) << 12)	/* Rd */
                    | ((tinstr & 0x0038) >> 3);	/* Rs */
                break;
            case t_shift:
                *ainstr |= ((tinstr & 0x0007) << 12)	/* Rd */
                    | ((tinstr & 0x0007) >> 0)	/* Rm */
                    | ((tinstr & 0x0038) << (8 - 3));	/* Rs */
                break;
            case t_neg:
                *ainstr |= ((tinstr & 0x0007) << 12)	/* Rd */
                    | ((tinstr & 0x0038) << (16 - 3));	/* Rn */
                break;
            case t_mul:
                *ainstr |= ((tinstr & 0x0007) << 16)	/* Rd */
                    | ((tinstr & 0x0007) << 8)	/* Rs */
                    | ((tinstr & 0x0038) >> 3);	/* Rm */
                break;
            }
        }
        else {
            /* Format 5 */
            ARMword Rd = ((tinstr & 0x0007) >> 0);
            ARMword Rs = ((tinstr & 0x0038) >> 3);
            if (tinstr & (1 << 7))
                Rd += 8;
            if (tinstr & (1 << 6))
                Rs += 8;
            switch ((tinstr & 0x03C0) >> 6) {
            case 0x0:	/* ADD Rd,Rd,Hs */
            case 0x1:	/* ADD Rd,Rd,Hs */
            case 0x2:	/* ADD Hd,Hd,Rs */
            case 0x3:	/* ADD Hd,Hd,Hs */
                *ainstr = 0xE0800000	/* base */
                    | (Rd << 16)	/* Rn */
                    | (Rd << 12)	/* Rd */
                    | (Rs << 0);	/* Rm */
                break;
            case 0x4:	/* CMP Rd,Hs */
            case 0x5:	/* CMP Rd,Hs */
            case 0x6:	/* CMP Hd,Rs */
            case 0x7:	/* CMP Hd,Hs */
                *ainstr = 0xE1500000	/* base */
                    | (Rd << 16)	/* Rn */
                    | (Rd << 12)	/* Rd */
                    | (Rs << 0);	/* Rm */
                break;
            case 0x8:	/* MOV Rd,Hs */
            case 0x9:	/* MOV Rd,Hs */
            case 0xA:	/* MOV Hd,Rs */
            case 0xB:	/* MOV Hd,Hs */
                *ainstr = 0xE1A00000	/* base */
                    | (Rd << 16)	/* Rn */
                    | (Rd << 12)	/* Rd */
                    | (Rs << 0);	/* Rm */
                break;
            case 0xC:	/* BX Rs */
            case 0xD:	/* BX Hs */
                *ainstr = 0xE12FFF10	/* base */
                    | ((tinstr & 0x0078) >> 3);	/* Rd */
                break;
            case 0xE:	/* BLX */
            case 0xF:	/* BLX */
                if (state->is_v5) {
                    *ainstr = 0xE1200030	/* base */
                        | (Rs << 0);	/* Rm */
                }
                else {
                    valid = t_undefined;
                }
                break;
            }
        }
        break;
    case 9:		/* LDR Rd,[PC,#imm8] */
        /* Format 6 */
        *ainstr = 0xE59F0000	/* base */
            | ((tinstr & 0x0700) << (12 - 8))	/* Rd */
            | ((tinstr & 0x00FF) << (2 - 0));	/* off8 */
        break;
    case 10:
    case 11:
        /* TODO: Format 7 and Format 8 perform the same ARM encoding, so
           the following could be merged into a single subset, saving on
           the following boolean: */
        if ((tinstr & (1 << 9)) == 0) {
            /* Format 7 */
            ARMword subset[4] = {
                0xE7800000,	/* STR  Rd,[Rb,Ro] */
                0xE7C00000,	/* STRB Rd,[Rb,Ro] */
                0xE7900000,	/* LDR  Rd,[Rb,Ro] */
                0xE7D00000	/* LDRB Rd,[Rb,Ro] */
            };
            *ainstr = subset[(tinstr & 0x0C00) >> 10]	/* base */
                | ((tinstr & 0x0007) << (12 - 0))	/* Rd */
                | ((tinstr & 0x0038) << (16 - 3))	/* Rb */
                | ((tinstr & 0x01C0) >> 6);	/* Ro */
        }
        else {
            /* Format 8 */
            ARMword subset[4] = {
                0xE18000B0,	/* STRH  Rd,[Rb,Ro] */
                0xE19000D0,	/* LDRSB Rd,[Rb,Ro] */
                0xE19000B0,	/* LDRH  Rd,[Rb,Ro] */
                0xE19000F0	/* LDRSH Rd,[Rb,Ro] */
            };
            *ainstr = subset[(tinstr & 0x0C00) >> 10]	/* base */
                | ((tinstr & 0x0007) << (12 - 0))	/* Rd */
                | ((tinstr & 0x0038) << (16 - 3))	/* Rb */
                | ((tinstr & 0x01C0) >> 6);	/* Ro */
        }
        break;
    case 12:		/* STR Rd,[Rb,#imm5] */
    case 13:		/* LDR Rd,[Rb,#imm5] */
    case 14:		/* STRB Rd,[Rb,#imm5] */
    case 15:		/* LDRB Rd,[Rb,#imm5] */
        /* Format 9 */
    {
        ARMword subset[4] = {
            0xE5800000,	/* STR  Rd,[Rb,#imm5] */
            0xE5900000,	/* LDR  Rd,[Rb,#imm5] */
            0xE5C00000,	/* STRB Rd,[Rb,#imm5] */
            0xE5D00000	/* LDRB Rd,[Rb,#imm5] */
        };
        /* The offset range defends on whether we are transferring a
           byte or word value: */
        *ainstr = subset[(tinstr & 0x1800) >> 11]	/* base */
            | ((tinstr & 0x0007) << (12 - 0))	/* Rd */
            | ((tinstr & 0x0038) << (16 - 3))	/* Rb */
            | ((tinstr & 0x07C0) >> (6 - ((tinstr & (1 << 12)) ? 0 : 2)));	/* off5 */
    }
    break;
    case 16:		/* STRH Rd,[Rb,#imm5] */
    case 17:		/* LDRH Rd,[Rb,#imm5] */
        /* Format 10 */
        *ainstr = ((tinstr & (1 << 11))	/* base */
            ? 0xE1D000B0	/* LDRH */
            : 0xE1C000B0)	/* STRH */
            | ((tinstr & 0x0007) << (12 - 0))	/* Rd */
            | ((tinstr & 0x0038) << (16 - 3))	/* Rb */
            | ((tinstr & 0x01C0) >> (6 - 1))	/* off5, low nibble */
            | ((tinstr & 0x0600) >> (9 - 8));	/* off5, high nibble */
        break;
    case 18:		/* STR Rd,[SP,#imm8] */
    case 19:		/* LDR Rd,[SP,#imm8] */
        /* Format 11 */
        *ainstr = ((tinstr & (1 << 11))	/* base */
            ? 0xE59D0000	/* LDR */
            : 0xE58D0000)	/* STR */
            | ((tinstr & 0x0700) << (12 - 8))	/* Rd */
            | ((tinstr & 0x00FF) << 2);	/* off8 */
        break;
    case 20:		/* ADD Rd,PC,#imm8 */
    case 21:		/* ADD Rd,SP,#imm8 */
        /* Format 12 */
        if ((tinstr & (1 << 11)) == 0) {
            /* NOTE: The PC value used here should by word aligned */
            /* We encode shift-left-by-2 in the rotate immediate field,
               so no shift of off8 is needed.  */
            *ainstr = 0xE28F0F00	/* base */
                | ((tinstr & 0x0700) << (12 - 8))	/* Rd */
                | (tinstr & 0x00FF);	/* off8 */
        }
        else {
            /* We encode shift-left-by-2 in the rotate immediate field,
               so no shift of off8 is needed.  */
            *ainstr = 0xE28D0F00	/* base */
                | ((tinstr & 0x0700) << (12 - 8))	/* Rd */
                | (tinstr & 0x00FF);	/* off8 */
        }
        break;
    case 22:
    case 23:
        if ((tinstr & 0x0F00) == 0x0000) {
            // NOTE: The instruction contains a shift left of 2 equivalent (implemented as ROR #30):
            *ainstr = ((tinstr & (1 << 7))  // base
                ? 0xE24DDF00             // SUB
                : 0xE28DDF00)            // ADD
                | (tinstr & 0x007F);         // off7
        }
        else if ((tinstr & 0x0F00) == 0x0e00)
            *ainstr = 0xEF000000 | SWI_Breakpoint;
        else {
            if ((tinstr & 0x600) == 0x400)
            {
                /* Format 14 */
                u32 subset[4] = {
                    0xE92D0000, /* STMDB sp!,{rlist} */
                    0xE92D4000, /* STMDB sp!,{rlist,lr} */
                    0xE8BD0000, /* LDMIA sp!,{rlist} */
                    0xE8BD8000 /* LDMIA sp!,{rlist,pc} */
                };
                *ainstr = subset[((tinstr & (1 << 11)) >> 10) | ((tinstr & (1 << 8)) >> 8)] /* base */
                    | (tinstr & 0x00FF); /* mask8 */
            }
            else
            {
                //e6bf1071 sxth r1, r1
                //e6af1071 sxtb r1, r1
                //e6ff1078 uxth r1, r8
                //e6ef1078 uxtb r1, r8

                u32 subset[4] = { //Bit 12 - 15 dest Bit 0 - 3 src
                    0xe6ff0070, /* uxth */
                    0xe6ef0070, /* uxtb */
                    0xe6bf0070, /* sxth */
                    0xe6af0070 /* sxtb */
                };

                if ((tinstr & 0xF00) == 0x200) //Bit(7) unsigned (set = sxt. cleared = uxt) Bit(6) byte (set = .xtb cleared = .xth) Bit 5-3 Rb src Bit 2-0 Rd dest
                {
                    *ainstr = subset[((tinstr & (0x3 << 6)) >> 6)] |
                        (tinstr & 0x7) << 12 |
                        (tinstr & 0x38) >> 3;
                }
                else if ((tinstr & 0x0FC0) == 0x0A00){
                    u32 Destr = (tinstr & 0x7);
                    u32 srcr = ((tinstr >> 3) & 0x7);
                    *ainstr = 0xE6BF0F30 | srcr | (Destr << 12);

                }
                else
                {
                    valid = t_undefined;
                    XDSERROR("unk thumb instr %04x", tinstr);
                }

            }
        }
        break;
    case 24:		/* STMIA */
    case 25:		/* LDMIA */
        /* Format 15 */
    {
        u32 Rb = (tinstr & 0x0700) >> 8;
        if ((1 << Rb)&tinstr) //no write back if the register is in the list
        {
            *ainstr = ((tinstr & (1 << 11))	/* base */
                ? 0xE8900000	/* LDMIA */
                : 0xE8800000)	/* STMIA */
                | ((tinstr & 0x0700) << (16 - 8))	/* Rb */
                | (tinstr & 0x00FF);	/* mask8 */
            break;
        }
        else
        {
            *ainstr = ((tinstr & (1 << 11))	/* base */
                ? 0xE8B00000	/* LDMIA */
                : 0xE8A00000)	/* STMIA */
                | ((tinstr & 0x0700) << (16 - 8))	/* Rb */
                | (tinstr & 0x00FF);	/* mask8 */
            break;
        }
    }
    case 26:		/* Bcc */
    case 27:		/* Bcc/SWI */
        if ((tinstr & 0x0F00) == 0x0F00) {
            if (tinstr == (ARMul_ABORTWORD & 0xffff) &&
                state->AbortAddr == pc) {
                *ainstr = ARMul_ABORTWORD;
                break;
            }
            /* Format 17 : SWI */
            *ainstr = 0xEF000000;
            /* Breakpoint must be handled specially.  */
            if ((tinstr & 0x00FF) == 0x18)
                *ainstr |= ((tinstr & 0x00FF) << 16);
            /* New breakpoint value.  See gdb/arm-tdep.c  */
            else if ((tinstr & 0x00FF) == 0xFE)
                *ainstr |= SWI_Breakpoint;
            else
                *ainstr |= (tinstr & 0x00FF);
        }
        else if ((tinstr & 0x0F00) != 0x0E00) {
            /* Format 16 */
            int doit = FALSE;
            /* TODO: Since we are doing a switch here, we could just add
               the SWI and undefined instruction checks into this
               switch to same on a couple of conditionals: */
            switch ((tinstr & 0x0F00) >> 8) {
            case EQ:
                doit = ZFLAG;
                break;
            case NE:
                doit = !ZFLAG;
                break;
            case VS:
                doit = VFLAG;
                break;
            case VC:
                doit = !VFLAG;
                break;
            case MI:
                doit = NFLAG;
                break;
            case PL:
                doit = !NFLAG;
                break;
            case CS:
                doit = CFLAG;
                break;
            case CC:
                doit = !CFLAG;
                break;
            case HI:
                doit = (CFLAG && !ZFLAG);
                break;
            case LS:
                doit = (!CFLAG || ZFLAG);
                break;
            case GE:
                doit = ((!NFLAG && !VFLAG)
                    || (NFLAG && VFLAG));
                break;
            case LT:
                doit = ((NFLAG && !VFLAG)
                    || (!NFLAG && VFLAG));
                break;
            case GT:
                doit = ((!NFLAG && !VFLAG && !ZFLAG)
                    || (NFLAG && VFLAG && !ZFLAG));
                break;
            case LE:
                doit = ((NFLAG && !VFLAG)
                    || (!NFLAG && VFLAG)) || ZFLAG;
                break;
            }
            if (doit) {
                state->Reg[15] = (pc + 4
                    + (((tinstr & 0x7F) << 1)
                    | ((tinstr & (1 << 7)) ?
                    0xFFFFFF00 : 0)));
                FLUSHPIPE;
            }
            valid = t_branch;
        }
        else		/* UNDEFINED : cc=1110(AL) uses different format */
            valid = t_undefined;
        break;
    case 28:		/* B */
        /* Format 18 */
        state->Reg[15] = (pc + 4 + (((tinstr & 0x3FF) << 1)
            | ((tinstr & (1 << 10)) ?
            0xFFFFF800 : 0)));
        FLUSHPIPE;
        valid = t_branch;
        break;
    case 29:
        if (tinstr & 0x1)
            valid = t_undefined;
        else{
            /* BLX 1 for armv5t and above */
            ARMword tmp = (pc + 2);
            state->Reg[15] =
                (state->Reg[14] + ((tinstr & 0x07FF) << 1)) & 0xFFFFFFFC;
            state->Reg[14] = (tmp | 1);
            CLEART;
            LOG("After  BLX(1),LR=0x%x,PC=0x%x, offset=0x%x", state->Reg[14], state->Reg[15], (tinstr & 0x7FF) << 1);
            valid = t_branch;
            FLUSHPIPE;
        }
        break;
    case 30:		/* BL instruction 1 */
        /* Format 19 */
        /* There is no single ARM instruction equivalent for this Thumb
           instruction. To keep the simulation simple (from the user
           perspective) we check if the following instruction is the
           second half of this BL, and if it is we simulate it
           immediately.  */
        state->Reg[14] = state->Reg[15]
            + (((tinstr & 0x07FF) << 12)
            | ((tinstr & (1 << 10)) ? 0xFF800000 : 0));
        valid = t_branch;	/* in-case we don't have the 2nd half */
        //tinstr = next_instr;	/* move the instruction down */
        //if (((tinstr & 0xF800) >> 11) != 31)
        //	break;	/* exit, since not correct instruction */
        /* else we fall through to process the second half of the BL */
        //pc += 2;	/* point the pc at the 2nd half */
        state->Reg[15] = pc + 2;
        FLUSHPIPE;
        break;
    case 31:		/* BL instruction 2 */
        /* Format 19 */
        /* There is no single ARM instruction equivalent for this
           instruction. Also, it should only ever be matched with the
           fmt19 "BL instruction 1" instruction. However, we do allow
           the simulation of it on its own, with undefined results if
           r14 is not suitably initialised.  */
    {
        ARMword tmp = (pc + 2);
        state->Reg[15] =
            (state->Reg[14] + ((tinstr & 0x07FF) << 1));
        state->Reg[14] = (tmp | 1);
        valid = t_branch;
        FLUSHPIPE;
    }
    break;
    }

    return valid;
}
