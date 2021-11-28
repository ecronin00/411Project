#include "mips-small-pipe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/************************************************************/
int main(int argc, char *argv[]) {
  short i;
  char line[MAXLINELENGTH];
  state_t state;
  FILE *filePtr;

  if (argc != 2) {
    printf("error: usage: %s <machine-code file>\n", argv[0]);
    return 1;
  }

  memset(&state, 0, sizeof(state_t));

  state.pc = state.cycles = 0;
  state.IFID.instr = state.IDEX.instr = state.EXMEM.instr = state.MEMWB.instr =
      state.WBEND.instr = NOPINSTRUCTION; /* nop */

  /* read machine-code file into instruction/data memory (starting at address 0)
   */

  filePtr = fopen(argv[1], "r");
  if (filePtr == NULL) {
    printf("error: can't open file %s\n", argv[1]);
    perror("fopen");
    exit(1);
  }

  for (state.numMemory = 0; fgets(line, MAXLINELENGTH, filePtr) != NULL;
       state.numMemory++) {
    if (sscanf(line, "%x", &state.dataMem[state.numMemory]) != 1) {
      printf("error in reading address %d\n", state.numMemory);
      exit(1);
    }
    state.instrMem[state.numMemory] = state.dataMem[state.numMemory];
    printf("memory[%d]=%x\n", state.numMemory, state.dataMem[state.numMemory]);
  }

  printf("%d memory words\n", state.numMemory);

  printf("\tinstruction memory:\n");
  for (i = 0; i < state.numMemory; i++) {
    printf("\t\tinstrMem[ %d ] = ", i);
    printInstruction(state.instrMem[i]);
  }

  run(&state);

  return 0;
}
/************************************************************/

/************************************************************/
void run(Pstate state) {
  state_t new;
  memset(&new, 0, sizeof(state_t));

  /* loop forever until HALT instruction */
  while (1) {

    printState(state);

    /* copy everything so all we have to do is make changes.
       (this is primarily for the memory and reg arrays) */
    memcpy(&new, state, sizeof(state_t));

    /* state is copied into variable "new"
       increment the cycle count by 1 for the new state */
    new.cycles++;


    /* IF is for most recent instruction, other stages are for previous
       instructions */

    /* when we go through the first iteration, all cycles except IF
       should do nothing (NOPINSTRUCTION) */ 
    
    /* --------------------- IF stage --------------------- */

    /* fetch the next instruction */
    new.IFID.instr = new.instrMem[new.pc / 4];
    
    /* gotta add 4 to the program counter */
    new.pc += 4;

    new.IFID.pcPlus1 += 4;
    
    /* i guess that pcPlus1 is to get the next instruction to be fetched? */
    
    /* --------------------- ID stage --------------------- */
    /* decode the instruction BEFORE the new instruction
     by grabbing the instruction from the IFID pipeline
     (where the instruction previously was) */

    new.IDEX.instr = state -> IFID.instr;

    new.IDEX.pcPlus1 = state->IFID.pcPlus1;
    new.IDEX.readRegA = new.reg[field_r1(new.IDEX.instr)];
    new.IDEX.readRegB = new.reg[field_r2(new.IDEX.instr)];;
    new.IDEX.offset = offset(new.IDEX.instr);
    
    /* check if this instruction is a register-register instruction */
    if (opcode(new.IDEX.instr) == REG_REG_OP) {

    }

    /* --------------------- EX stage --------------------- */

    new.EXMEM.instr = state -> IDEX.instr;
    
    if (new.EXMEM.instr != NOPINSTRUCTION && opcode(new.EXMEM.instr) == ADDI_OP) {
      
      new.EXMEM.aluResult = offset(new.EXMEM.instr) + new.reg[field_r1(new.EXMEM.instr)];
      
    } else {

      new.EXMEM.aluResult = 0;
      
    }
    

    /* --------------------- MEM stage --------------------- */

    new.MEMWB.instr = state -> EXMEM.instr;

    
    if (opcode(new.MEMWB.instr) != HALT_OP && opcode(new.MEMWB.instr) == ADDI_OP) {
      new.MEMWB.writeData = state -> EXMEM.aluResult;
    } else if (opcode(new.MEMWB.instr) == HALT_OP) {
      new.MEMWB.writeData = 0;
    }
    

    /* --------------------- WB stage --------------------- */

    new.WBEND.instr = state -> MEMWB.instr;

    if (opcode(new.WBEND.instr) == ADDI_OP) {
      new.WBEND.writeData = state -> MEMWB.writeData;
      new.reg[field_r2(new.WBEND.instr)] = new.WBEND.writeData;
      
    } else if (opcode(new.WBEND.instr) == HALT_OP) {
      printf("machine halted\n");
      printf("total of %d cycles executed\n", state -> cycles);
      exit(0);
    }

    /* --------------------- end stage --------------------- */

    /* transfer new state into current state */
    memcpy(state, &new, sizeof(state_t));
  }
}
/************************************************************/

/************************************************************/
int opcode(int instruction) { return (instruction >> OP_SHIFT) & OP_MASK; }
/************************************************************/

/************************************************************/
int func(int instruction) { return (instruction & FUNC_MASK); }
/************************************************************/

/************************************************************/
int field_r1(int instruction) { return (instruction >> R1_SHIFT) & REG_MASK; }
/************************************************************/

/************************************************************/
int field_r2(int instruction) { return (instruction >> R2_SHIFT) & REG_MASK; }
/************************************************************/

/************************************************************/
int field_r3(int instruction) { return (instruction >> R3_SHIFT) & REG_MASK; }
/************************************************************/

/************************************************************/
int field_imm(int instruction) { return (instruction & IMMEDIATE_MASK); }
/************************************************************/

/************************************************************/
int offset(int instruction) {
  /* only used for lw, sw, beqz */
  return convertNum(field_imm(instruction));
}
/************************************************************/

/************************************************************/
int convertNum(int num) {
  /* convert a 16 bit number into a 32-bit Sun number */
  if (num & 0x8000) {
    num -= 65536;
  }
  return (num);
}
/************************************************************/

/************************************************************/
void printState(Pstate state) {
  short i;
  printf("@@@\nstate before cycle %d starts\n", state->cycles);
  printf("\tpc %d\n", state->pc);

  printf("\tdata memory:\n");
  for (i = 0; i < state->numMemory; i++) {
    printf("\t\tdataMem[ %d ] %d\n", i, state->dataMem[i]);
  }
  printf("\tregisters:\n");
  for (i = 0; i < NUMREGS; i++) {
    printf("\t\treg[ %d ] %d\n", i, state->reg[i]);
  }
  printf("\tIFID:\n");
  printf("\t\tinstruction ");
  printInstruction(state->IFID.instr);
  printf("\t\tpcPlus1 %d\n", state->IFID.pcPlus1);
  printf("\tIDEX:\n");
  printf("\t\tinstruction ");
  printInstruction(state->IDEX.instr);
  printf("\t\tpcPlus1 %d\n", state->IDEX.pcPlus1);
  printf("\t\treadRegA %d\n", state->IDEX.readRegA);
  printf("\t\treadRegB %d\n", state->IDEX.readRegB);
  printf("\t\toffset %d\n", state->IDEX.offset);
  printf("\tEXMEM:\n");
  printf("\t\tinstruction ");
  printInstruction(state->EXMEM.instr);
  printf("\t\taluResult %d\n", state->EXMEM.aluResult);
  printf("\t\treadRegB %d\n", state->EXMEM.readRegB);
  printf("\tMEMWB:\n");
  printf("\t\tinstruction ");
  printInstruction(state->MEMWB.instr);
  printf("\t\twriteData %d\n", state->MEMWB.writeData);
  printf("\tWBEND:\n");
  printf("\t\tinstruction ");
  printInstruction(state->WBEND.instr);
  printf("\t\twriteData %d\n", state->WBEND.writeData);
}
/************************************************************/

/************************************************************/
void printInstruction(int instr) {

  if (opcode(instr) == REG_REG_OP) {

    if (func(instr) == ADD_FUNC) {
      print_rtype(instr, "add");
    } else if (func(instr) == SLL_FUNC) {
      print_rtype(instr, "sll");
    } else if (func(instr) == SRL_FUNC) {
      print_rtype(instr, "srl");
    } else if (func(instr) == SUB_FUNC) {
      print_rtype(instr, "sub");
    } else if (func(instr) == AND_FUNC) {
      print_rtype(instr, "and");
    } else if (func(instr) == OR_FUNC) {
      print_rtype(instr, "or");
    } else {
      printf("data: %d\n", instr);
    }

  } else if (opcode(instr) == ADDI_OP) {
    print_itype(instr, "addi");
  } else if (opcode(instr) == LW_OP) {
    print_itype(instr, "lw");
  } else if (opcode(instr) == SW_OP) {
    print_itype(instr, "sw");
  } else if (opcode(instr) == BEQZ_OP) {
    print_itype(instr, "beqz");
  } else if (opcode(instr) == HALT_OP) {
    printf("halt\n");
  } else {
    printf("data: %d\n", instr);
  }
}
/************************************************************/

/************************************************************/
void print_rtype(int instr, const char *name) {
  printf("%s %d %d %d\n", name, field_r3(instr), field_r1(instr),
         field_r2(instr));
}
/************************************************************/

/************************************************************/
void print_itype(int instr, const char *name) {
  printf("%s %d %d %d\n", name, field_r2(instr), field_r1(instr),
         offset(instr));
}
/************************************************************/
