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
  
  int reg1 = 0;
  int reg2 = 0;
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
    
    /* test the offset for branch instructions  */
    if (opcode(new.IFID.instr) == BEQZ_OP) {
      
      if (offset(new.IFID.instr) > 0) {
	
	new.IFID.pcPlus1 = new.pc;
	
      } else {
	
	new.pc = new.pc + offset(new.IFID.instr);
	new.IFID.pcPlus1 = new.pc - offset(new.IFID.instr);
	
      }
      
    } else {
      
      new.IFID.pcPlus1 = new.pc;
      
    }
    
    
    /* --------------------- ID stage --------------------- */
    
    new.IDEX.instr = state -> IFID.instr;
    
    new.IDEX.pcPlus1 = state->IFID.pcPlus1;
    new.IDEX.readRegA = new.reg[field_r1(new.IDEX.instr)];
    new.IDEX.readRegB = new.reg[field_r2(new.IDEX.instr)];;
    new.IDEX.offset = offset(new.IDEX.instr);
    
    /* dont forget that if this instruction is R type and previous instruction is LW
       and there is a dependence then you have to stall one cycle 
       actually same thing goes for ADDI if theres a dependence  */
    if ((opcode(new.IDEX.instr) == REG_REG_OP) && (opcode(state -> IDEX.instr) == LW_OP)) { 
      if (field_r1(new.IDEX.instr) == field_r2(state -> IDEX.instr) ||
	  (field_r2(new.IDEX.instr) == field_r2(state -> IDEX.instr))) {

	printf("LOAD STALL INCOMING!!!\n");
	
	new.IDEX.instr = NOPINSTRUCTION;
	new.IDEX.pcPlus1 = 0;
	new.IDEX.offset = offset(NOPINSTRUCTION);
	
	new.IFID.instr = state -> IFID.instr;
	new.pc -= 4;
	new.IFID.pcPlus1 = new.pc;
	
      }
    }

    /* need to check for stalls when last instruction is LW and this one is LW
       using an offset of the register the previous instruction is loading into */

    if ((opcode(new.IDEX.instr) != REG_REG_OP) &&
	(opcode(new.IDEX.instr) != HALT_OP) && (opcode(state -> IDEX.instr) == LW_OP)) {
      if (field_r1(new.IDEX.instr) == field_r2(state -> IDEX.instr)) {

	/* printf("LOAD STALL INCOMING!!!\n"); */

	new.IDEX.instr = NOPINSTRUCTION;
	new.IDEX.pcPlus1 = 0;
	new.IDEX.offset = offset(NOPINSTRUCTION);

	new.IFID.instr = state -> IFID.instr;
	new.pc -= 4;
	new.IFID.pcPlus1 = new.pc;

      }
    }
    
    /* --------------------- EX stage --------------------- */
    
    
    /* there are three places that you can forward from (EXMEM, MEMWB, WBEND)
       need to check if the instructions you are forwarding from are R type or I type
       because that changes the fields you need to use */
    
    new.EXMEM.instr = state -> IDEX.instr;
    
    reg1 = new.reg[field_r1(new.EXMEM.instr)];
    
    reg2 = new.reg[field_r2(new.EXMEM.instr)];
    
    new.EXMEM.readRegB = 0;
    new.EXMEM.aluResult = 0;
    
    /* check if the instruction we are forwarding from is an I type instruction
       from MEMWB */
    if (opcode(new.EXMEM.instr) != HALT_OP ) {
      if (state -> MEMWB.instr != NOPINSTRUCTION &&
	  opcode(state -> MEMWB.instr) != REG_REG_OP &&
	  opcode(state -> MEMWB.instr) != SW_OP &&
	  opcode(state -> MEMWB.instr) != BEQZ_OP) {
	
	if (field_r1(new.EXMEM.instr) == field_r2(state -> MEMWB.instr)) {

	  reg1 = state -> MEMWB.writeData;
	  
	}
	
	if (opcode(new.EXMEM.instr) == REG_REG_OP) {
	  
	  if (field_r2(new.EXMEM.instr) == field_r2(state -> MEMWB.instr)) {

	    reg2 = state -> MEMWB.writeData;
	    
	  }
	  
	}  
	
	/* check if the instruction we are forwarding from is an R type instruction
	   from MEMWB */
      } else if (state -> MEMWB.instr != NOPINSTRUCTION &&
		 opcode(state -> MEMWB.instr) == REG_REG_OP &&
		 opcode(state -> MEMWB.instr) != SW_OP) {
	
	if (field_r1(new.EXMEM.instr) == field_r3(state -> MEMWB.instr)) {

	  reg1 = state -> MEMWB.writeData;
	  
	}
	
	if (opcode(new.EXMEM.instr) == REG_REG_OP) {
	  
	  if (field_r2(new.EXMEM.instr) == field_r3(state -> MEMWB.instr)) {

	    reg2 = state -> MEMWB.writeData;
	    
	  }
	}
      }
      
      /* check if instruction we are forwarding from is I type from EXMEM */
      if (state -> EXMEM.instr != NOPINSTRUCTION &&
	  opcode(state -> EXMEM.instr) != REG_REG_OP &&
	  opcode(state -> EXMEM.instr) != SW_OP &&
	  opcode(state -> EXMEM.instr) != BEQZ_OP) {
	
	if (field_r1(new.EXMEM.instr) == field_r2(state -> EXMEM.instr)) {

	  reg1 = state -> EXMEM.aluResult;
	  
	}
	
	if (opcode(new.EXMEM.instr) == REG_REG_OP) {
	  
	  if (field_r2(new.EXMEM.instr) == field_r2(state -> EXMEM.instr)) {
	    
	    reg2 = state -> EXMEM.aluResult;
	    
	  }
	}
	
	/* check if instruction we are forwarding from is R type from EXMEM */
      } else if (state -> EXMEM.instr != NOPINSTRUCTION &&
		 opcode(state -> EXMEM.instr) == REG_REG_OP &&
		 opcode(state -> EXMEM.instr) != SW_OP) {
	
	/* if this instruction is SW, just take the previous aluresult into reg2 */
	if (opcode(new.EXMEM.instr) == SW_OP) {
	  
	  reg2 = state -> EXMEM.aluResult;

	}
	
	if (field_r1(new.EXMEM.instr) == field_r3(state -> EXMEM.instr)) {
	  
	  reg1 = state -> EXMEM.aluResult;
	  
	}
	
	if (opcode(new.EXMEM.instr) == REG_REG_OP) {
	  
	  if (field_r2(new.EXMEM.instr) == field_r3(state -> EXMEM.instr)) {
	    
	    reg2 = state -> EXMEM.aluResult;
	    
	  }	
	} 
      }
    }
    
    /* in this part we actually do the execution, no more forwarding after here */
    if (new.EXMEM.instr != NOPINSTRUCTION) {
      if (opcode(new.EXMEM.instr) == ADDI_OP) {

	new.EXMEM.aluResult = offset(new.EXMEM.instr) + new.reg[field_r1(new.EXMEM.instr)];
	new.EXMEM.readRegB = state -> IDEX.readRegB;
	
      } else if (opcode(new.EXMEM.instr) == LW_OP) {

	new.EXMEM.aluResult = reg1 + field_imm(new.EXMEM.instr);
	new.EXMEM.readRegB = new.reg[field_r2(new.EXMEM.instr)];
	
      } else if (opcode(new.EXMEM.instr) == SW_OP) {

	/* printf("REG1 VALUE: %d\n", reg1); */
	new.EXMEM.aluResult = reg1 + field_imm(new.EXMEM.instr);
	new.EXMEM.readRegB = new.reg[field_r2(new.EXMEM.instr)];
	
      } else if (opcode(new.EXMEM.instr) == BEQZ_OP) {

	/* if offset > 0 and reg1 == 0 then we predicted wrong 
	   if offset < 0 and reg1 != 0 then we predicted wrong */
	if ((state->IDEX.offset > 0 && reg1 == 0) ||
	    (state->IDEX.offset < 0 && reg1 != 0)) {

	  /* now that we know we predicted wrong, replace last 2 instructions with
             NOPINSTRUCTION and adjust to fetch the proper instruction next */
	  new.IFID.instr = NOPINSTRUCTION;
	  new.IDEX.instr = NOPINSTRUCTION;
	  new.IDEX.readRegA = 0;
	  new.IDEX.readRegB = 0;
	  new.pc = state -> IDEX.offset + state -> IDEX.pcPlus1;
	  new.IFID.pcPlus1 = 0;
	  new.IDEX.pcPlus1 = 0;
	  reg1 = 0;
	  reg2 = 0;
	  new.IDEX.offset = 32;

	  new.EXMEM.aluResult = state->IDEX.pcPlus1 + offset(new.EXMEM.instr);
	  new.EXMEM.readRegB = new.reg[field_r2(new.EXMEM.instr)];

	  /* this is for when we get it right, man this part is so much nicer than the last */
	} else {

	  new.EXMEM.readRegB = new.reg[field_r2(new.EXMEM.instr)];
	  new.EXMEM.aluResult = state -> IDEX.pcPlus1 + offset(new.EXMEM.instr);
	
	}

      } else if (opcode(new.EXMEM.instr) == REG_REG_OP) {
	
	if (func(new.EXMEM.instr) == ADD_FUNC) {
	  
	  new.EXMEM.aluResult = reg1 + reg2;
	  new.EXMEM.readRegB = reg2;
	  
	} else if (func(new.EXMEM.instr) == SUB_FUNC) {
	  
	  new.EXMEM.aluResult = reg1 - reg2;
	  new.EXMEM.readRegB = reg2;
	  
	} else if (func(new.EXMEM.instr) == AND_FUNC) {
	  
	  new.EXMEM.aluResult = reg1 & reg2;
	  new.EXMEM.readRegB = reg2;
	  
	} else if (func(new.EXMEM.instr) == OR_FUNC) {
	  
	  new.EXMEM.aluResult = reg1 | reg2;
	  new.EXMEM.readRegB = reg2;
	  
	} else if (func(new.EXMEM.instr) == SLL_FUNC) {
	  
	  new.EXMEM.aluResult = reg1 << reg2;
	  new.EXMEM.readRegB = reg2;
	  
	} else if (func(new.EXMEM.instr) == SRL_FUNC) {
	  
	  new.EXMEM.aluResult = reg1 >> reg2;
	  new.EXMEM.readRegB = reg2;
	  
	} else {

	  new.EXMEM.aluResult = 0;
	  
	}
	reg1 = 0;
	reg2 = 0;
      }
    }
      
    /* --------------------- MEM stage --------------------- */
    
    new.MEMWB.instr = state -> EXMEM.instr;
    new.MEMWB.writeData = 0;
    
    if (opcode(new.MEMWB.instr) != HALT_OP) {
      if (opcode(new.MEMWB.instr) == ADDI_OP ||
	  opcode(new.MEMWB.instr) == REG_REG_OP ||
	  opcode(new.MEMWB.instr) == BEQZ_OP) {
	
	new.MEMWB.writeData = state -> EXMEM.aluResult;
	
      } else if (opcode(new.MEMWB.instr) == LW_OP) {
	
	new.MEMWB.writeData = state -> dataMem[state -> EXMEM.aluResult / 4];
	
      } else if (opcode(new.MEMWB.instr) == SW_OP) {
	
	new.MEMWB.writeData = state->EXMEM.readRegB;
	new.dataMem[(new.reg[field_r1(new.MEMWB.instr)] +
		     field_imm(new.MEMWB.instr))/4] = new.MEMWB.writeData;
	
      } else if (opcode(new.MEMWB.instr) == HALT_OP) {
	new.MEMWB.writeData = 0;
      }
    }
    
    
    /* --------------------- WB stage --------------------- */
    
    new.WBEND.instr = state -> MEMWB.instr;
    new.WBEND.writeData = 0;
    
    if (opcode(new.WBEND.instr) == ADDI_OP) {
      new.WBEND.writeData = state -> MEMWB.writeData;
      new.reg[field_r2(new.WBEND.instr)] = new.WBEND.writeData;
      
    } else if (opcode(new.WBEND.instr) == REG_REG_OP) {
      
      new.WBEND.writeData = state -> MEMWB.writeData;
      new.reg[field_r3(new.WBEND.instr)] = state -> MEMWB.writeData;
      
    } else if (opcode(new.WBEND.instr) == LW_OP) {
      
      new.WBEND.writeData = state -> MEMWB.writeData;
      
      new.reg[field_r2(new.WBEND.instr)] = new.WBEND.writeData;
      
    } else if (opcode(new.WBEND.instr) == SW_OP) {
      
      new.WBEND.writeData = state -> MEMWB.writeData;
      
    } else if (opcode(new.WBEND.instr) == BEQZ_OP) {

      new.WBEND.writeData = state -> MEMWB.writeData;

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
