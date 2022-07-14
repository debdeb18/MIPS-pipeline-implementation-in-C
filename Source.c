#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include "Source.h"

#define MEMORY_STRUCT //memory filled process
//#define STAGES_CHANGE //iterated process
//#define DEBUG //mips instuction decode

int main(int argc, char* argv[])
{
	//signal that cross over the stage backward
	int ALUOutM = 0, PCBranchD = 0, PCSrcD = 0, ResultW = 0;

	//open file and load to memory
	int ret;
	FILE* file;
	char* filename;

	if(argc == 2){
		filename = argv[1];
	} else {
		filename = "simple3.bin";
	}

	if ((file = fopen(filename, "rb")) == NULL) {
		perror("cannot open file");
		exit(1);
	}
	load_instruction(file);
	fclose(file); //close file

	//initialize to start
	initialize();
	k = 0, clk = 0;

	//start loop
	for(;;) {
		start_t = clock();
		write_back(&ResultW);
		update_clk_t();

		memory_access(&ALUOutM);
		update_clk_t();

		hazard_det_unit(); //check for hazard
		execution(&ALUOutM, &ResultW);
		update_clk_t();

		instruction_decode(&ALUOutM, &PCBranchD, &PCSrcD);
		update_clk_t();

		stall_unit(); //check if stalling is needed
		instruction_fetch(&PCBranchD, &PCSrcD);
		update_clk_t();

		clock_cycle(); //keep track of the clock cycle
#if defined DEBUG || defined STAGES_CHANGE
		printf("\n");
#endif
	}

	return 0;
}

//load instruction from file to memory
void load_instruction(FILE* fd) {
	size_t ret = 0;
	int mips_val, i = 0;
	int mem_val;

	do
	{
		mips_val = 0;
		ret = fread(&mips_val, 1, 4, fd);
		mem_val = swap(mips_val);
		data_memory[i] = mem_val;

#ifdef MEMORY_STRUCT
		printf("(%d) load Mem[%x] pa: 0x%p val: 0x%x\n", (int)ret, i, &data_memory[i], data_memory[i]);
#endif
		i++;
	} while (ret == 4);
}


//instruction fetch phase
void instruction_fetch(int* PCBranchD, int* PCSrcD) {
	int PC = mux2(*PCBranchD, if_id_l.pc, *PCSrcD);
	
	*PCSrcD = 0;
	check(PC, "program end"); //if pc == 0xFFFFFFFF, end program

	int IR = data_memory[PC / 4];

#ifdef STAGES_CHANGE
	printf("[%d] PC: %x\t\t --> FET: %08x\n", k + 1, PC, IR);
#endif 


	if (hu.StallF == 1) {
		instr_info.enable = 0;
		return; //do not update if/id register if stalled
	}

	if_id_l.instruction = IR;
	if_id_l.pc = PC + 4;
	k++;
	instr_info.enable = 1;
	if_id_l.i = k;
}

//instruction decode phase
void instruction_decode(int* ALUOutM, int* PCBranchD, int* PCSrcD) {
	id_ex_l.i = if_id_l.i;
	int instr_rd = if_id_l.instruction;
	int op = bit(instr_rd, 6, 27);

	//instruction is a nop
	if (instr_rd == 0x0) {
#ifdef STAGES_CHANGE
		printf("[%d] id nop\n", if_id_l.i);
#endif
		if (instr_info.enable) {
			instr_info.nop++;
		}
		flush_E();
		return;
	}
	
	//an instruction after branch instruction
	if ((*PCSrcD == 1) && (op != 0x3)) {
#ifdef STAGES_CHANGE
		printf("[%d] id flushed\n", if_id_l.i);
#endif
		if_id_l.instruction = 0x0;
		flush_E();
		id_ex_l.i = 0;
		return;
	}

	int rs, funct, control_unit;
	rs = bit(instr_rd, 5, 22);
	funct = bit(instr_rd, 6, 1);

	//assigning control signals based on instruction
	control_unit = set_control_signal(op, funct);

	int imm = bit(instr_rd, 16, 1);
	imm = check_sign_extend(imm, op);

	//jump or jr instruction
	if (bit(control_unit, 1, 12) && (op != 0x3)) {
		if (bit(control_unit, 1, 11) == 1) {
			*PCBranchD = reg[rs];
#ifdef STAGES_CHANGE
			printf("[%d] instruction is jump to register\n", if_id_l.i);
#endif
		}
		else {
			*PCBranchD = bit(if_id_l.pc, 4, 29) + (imm << 2);
#ifdef STAGES_CHANGE
			printf("[%d] instruction is jump\n", if_id_l.i);
#endif
			if (instr_info.enable) {
				instr_info.j_type_count++;
			}
		}
		*PCSrcD = 1;
		flush_E();
		return;
	}

	*PCBranchD = if_id_l.pc + (short)((int32_t)imm << 2);
	int Branch, rt, rs_val, rt_val;
	Branch = bit(control_unit, 1, 10);
	rt = bit(instr_rd, 5, 17);
	rs_val = mux2(*ALUOutM, reg[rs], hu.ForwardAD);
	rt_val = mux2(*ALUOutM, reg[rt], hu.ForwardBD);

	hu.BranchD = Branch;
	if (instr_info.enable && Branch) {
		instr_info.branch++;
	}

	//early branch resolution
	if (Branch && ((bit(control_unit, 1, 13) && (rs_val != rt_val)) || (!bit(control_unit, 1, 13) && (rs_val == rt_val)))) {
		*PCSrcD = 1;
#ifdef STAGES_CHANGE
		printf("[%d] instruction is branch to %x\n", if_id_l.i, *PCBranchD);
#endif
		flush_E();
		if (instr_info.enable) {
			instr_info.branch_taken++;
		}
		return;
	}

	int rd, shamt;
	rd = bit(instr_rd, 5, 12);
	shamt = bit(instr_rd, 5, 7);
	//jal instruction
	if (op == 0x3) {
		*PCBranchD = bit(if_id_l.pc, 4, 29) + (imm << 2);
		*PCSrcD = 1;
#ifdef STAGES_CHANGE
		printf("[%d] instruction is jump and link\n", if_id_l.i);
#endif
		//skipping the actual necessity of using mux
		rd = 31;
		rs_val = if_id_l.pc + 4;
	}
	else {
#ifdef STAGES_CHANGE
		printf("[%d] op: %x	rs: %d	rt: %d	rd: %d	funct: %x	imm: %x\n", if_id_l.i, op, rs, rt, rd, funct, imm);
#endif
	}

	//lui instruction
	if (op == 0xf) {
		id_ex_l.sign_ext_imm = (imm << 16);
	}
	else {
		id_ex_l.sign_ext_imm = (int32_t)imm;
	}

	//hazard and id/ex latch update
	hu.MemWriteD = bit(bit(control_unit, 2, 3), 1, 1);
	id_ex_l.rs_val = rs_val;
	id_ex_l.rt_val = rt_val;
	id_ex_l.rs = rs;
	id_ex_l.rt = rt;
	id_ex_l.rd = rd;
	id_ex_l.shamt = shamt;
	id_ex_l.control_unit_wb = bit(control_unit, 2, 1);
	id_ex_l.control_unit_m = bit(control_unit, 2, 3);
	id_ex_l.control_unit_ex = bit(control_unit, 5, 5);
}

void execution(int* ALUOutM, int* ResultW) {
	//check if EX is flushed
	if (hu.FlushE == 1) {
		flush_E();
#ifdef STAGES_CHANGE
		printf("[%d] ex flushed\n", 0);
#endif
		ex_mem_l.i = 0;
	}
	else {
#ifdef STAGES_CHANGE
		printf("[%d] executed\n", id_ex_l.i);
#endif
		ex_mem_l.i = id_ex_l.i;
	}

	int ALUSrc, ALUcontrol, RegDst;
	ALUSrc = bit(id_ex_l.control_unit_ex, 1, 1);
	ALUcontrol = bit(id_ex_l.control_unit_ex, 3, 2);
	RegDst = bit(id_ex_l.control_unit_ex, 1, 5);

	//determine the input for ALU
	int input_one = mux3(*ResultW, *ALUOutM, id_ex_l.rs_val, hu.ForwardAE);
	int write_data = mux3(*ResultW, *ALUOutM, id_ex_l.rt_val, hu.ForwardBE);
	int input_two = mux2(id_ex_l.sign_ext_imm, write_data, ALUSrc);
	
	//ALU calculation
	int ALUOut = ALU_calculator(input_one, input_two, id_ex_l.shamt, ALUcontrol);

	int write_reg = mux2(id_ex_l.rd, id_ex_l.rt, RegDst);

	//hazard and ex/mem latch update
	hu.WriteRegE = write_reg;
	hu.MemtoRegE = bit(id_ex_l.control_unit_wb, 1, 1);
	hu.RegWriteE = bit(id_ex_l.control_unit_wb, 1, 2);
	ex_mem_l.alu_out = ALUOut;
	ex_mem_l.rt = write_data;
	ex_mem_l.write_reg = write_reg;
	ex_mem_l.control_unit_m = id_ex_l.control_unit_m;
	ex_mem_l.control_unit_wb = id_ex_l.control_unit_wb;
}

void memory_access(int* ALUOutM) {
	*ALUOutM = ex_mem_l.alu_out;
	int memWrite = bit(ex_mem_l.control_unit_m, 1, 1);
	int memRead = bit(ex_mem_l.control_unit_m, 1, 2);
	int read_data = 0;

	//check if instruction is lw or sw or neither
	if (memWrite == 1) {
#ifdef STAGES_CHANGE
		printf("[%d] memory updated\t --> 0x%x: 0x%x\n", ex_mem_l.i, ex_mem_l.alu_out, ex_mem_l.rt);
#endif
		data_memory[ex_mem_l.alu_out >> 2] = ex_mem_l.rt;
	}
	else if (memRead == 1) {
		read_data = data_memory[ex_mem_l.alu_out/ sizeof(unsigned int)];
#ifdef STAGES_CHANGE
		printf("[%d] data read --> %x = %x\n", ex_mem_l.i, ex_mem_l.alu_out, read_data);
#endif
	}
	else {
		read_data = 0;
#ifdef STAGES_CHANGE
		printf("[%d] mem nop\n", ex_mem_l.i);
#endif
	}

	//update hazard and mem/wb lach
	hu.RegWriteM = bit(ex_mem_l.control_unit_wb, 1, 2);
	mem_wb_l.alu_out = ex_mem_l.alu_out;
	mem_wb_l.write_reg = ex_mem_l.write_reg;
	mem_wb_l.read_data = read_data;
	mem_wb_l.control_unit_wb = ex_mem_l.control_unit_wb;
	mem_wb_l.i = ex_mem_l.i;
}

void write_back(int* ResultW) {
	int MemtoReg = bit(mem_wb_l.control_unit_wb, 1, 1);
	int RegWrite = bit(mem_wb_l.control_unit_wb, 1, 2);

	*ResultW = mux2(mem_wb_l.read_data, mem_wb_l.alu_out, MemtoReg);

	//check if write back is required
	if (RegWrite == 1) {
		reg[mem_wb_l.write_reg] = *ResultW;
#ifdef STAGES_CHANGE
		printf("[%d] register updated --> $%d: 0x%x\n", mem_wb_l.i, mem_wb_l.write_reg, *ResultW);
#endif
	}
	else {
#ifdef STAGES_CHANGE
		printf("[%d] wb nop\n", mem_wb_l.i);
#endif
	}

	//update hazard
	hu.RegWriteW = RegWrite;
}

//keeping the most time consuming stage in a single clock cycle
void update_clk_t() {
	clock_t temp_t = end_t;
	end_t = clock() - start_t;
	if (end_t < temp_t) {
		end_t = temp_t;
	}
	start_t = clock();
}

//clock cycle counter and
//keeping the most time consuming clock cycle throughout the program
void clock_cycle() {
	clk++;
	clock_t temp_t = clk_t;
	clk_t = end_t;
	if (clk_t < temp_t) {
		clk_t = temp_t;
	}
}

void initialize() {
	memset(reg, 0, sizeof(reg));
	reg[31] = 0xFFFFFFFF; //return address #31 = -1
	reg[29] = (unsigned int)0x400000; //stack pointer
	return;
}

//mux with 2 input
int mux2(int opnd1, int opnd2, int s) {
	if (s == 1)
		return opnd1;
	else
		return opnd2;
}

//mux with 3 input
int mux3(int opnd1, int opnd2, int opnd3, int s) {
	if (s == 1)
		return opnd1;
	else if (s == 2)
		return opnd2;
	else //s == 0
		return opnd3;
}

//extract a certain bit from hex number
int bit(int number, int numBit, int start)
{
	return (((1 << numBit) - 1) & (number >> (start - 1)));
}

//deal with big-little endian
int swap(int a) {
	a = ((a & (0x0000FFFF)) << 16) | ((a & (0xFFFF0000)) >> 16);
	a = ((a & (0x00FF00FF)) << 8) | ((a & (0xFF00FF00)) >> 8);
	return a;
}

//check negative or positive immediate value
int check_sign_extend(unsigned int imm, int opcode) {
	if ((opcode == 0xc) | (opcode == 0xd)) {
		imm = (unsigned int)imm;
	}
	else {
		if (imm << 15) {
			imm = (short)imm;
		}
		imm = (unsigned int)imm;
	}
	return imm;
}

//check end of program
int check(int exp, const char* msg) {
	if (exp == (-1)) {
		if (k == mem_wb_l.i) {
			print_output();
			perror(msg);
			exit(1);
		}
		else {
			hu.StallF = 1;
		}
	}
	return exp;
}

int print_output() {
	printf("-------------------------------------\n");
	printf("final result:\t\t $v0: %d\n", reg[2]);
	printf("instructions executed:\nTotal\t\t\t --> %d instructions\n", k);
	printf("nop instruction\t\t --> %d instructions\n", instr_info.nop);
	printf("R-type instruction\t --> %d instructions\n", instr_info.r_type_count);
	printf("I-type instruction\t --> %d instructions\n", instr_info.i_type_count);
	printf("J-type instruction\t --> %d instructions\n", instr_info.j_type_count);
	printf("Memory instruction\t --> %d instructions\n", instr_info.mem_instr_count);
	printf("Branch Not Taken\t --> %d out of %d branch instructions\n\n", instr_info.branch - instr_info.branch_taken, instr_info.branch);
	printf("executed in %d clock cycle for %f sec\n", clk, ((double)clk_t * clk / CLOCKS_PER_SEC));
#ifdef DEBUG
	printf("longest clock cycle is %f sec\n", (double)clk_t / CLOCKS_PER_SEC);
//	for (int i = 0; i < clk; i++) {
//		printf("clk %d = %f sec\n", i + 1, (double)temp[i + 1] / CLOCKS_PER_SEC);
//	}
#endif
	printf("-------------------------------------\n");
	return 0;
}

//ALU of EX stage
int ALU_calculator(int opnd1, int opnd2, int E, int control) {
	if ((opnd1 == 0) && (opnd2 == 0)) {
		return 0;
	}
	int result = 0;
	switch (control)
	{
	case 0:
		result = opnd1 & opnd2;

#ifdef DEBUG
		printf("and --> 0x%x & 0x%x = 0x%x\n", opnd1, opnd2, result);
#endif // DEBUG

		break;
	case 1:
		result = opnd1 | opnd2;
#ifdef DEBUG
		printf("or --> 0x%x | 0x%x = 0x%x\n", opnd1, opnd2, result);
#endif // DEBUG
		break;
	case 2:
		result = opnd1 + opnd2;
#ifdef DEBUG
		printf("add --> 0x%x + 0x%x = 0x%x\n", opnd1, opnd2, result);
#endif // DEBUG
		break;
	case 3:
		//not
		break;
	case 4:
		result = opnd2 << E;
#ifdef DEBUG
		printf("shift left --> 0x%x << %d = 0x%x\n", opnd2, E, result);
#endif // DEBUG
		break;
	case 5:
		result = opnd2 >> E;
#ifdef DEBUG
		printf("shift right --> 0x%x >> %d = 0x%x\n", opnd2, E, result);
#endif // DEBUG
		break;
	case 6:
		result = opnd1 - opnd2;
#ifdef DEBUG
		printf("sub --> 0x%x - 0x%x = 0x%x\n", opnd1, opnd2, result);
#endif // DEBUG
		break;
	case 7:
		result = (opnd1 < opnd2) ? 1 : 0;
#ifdef DEBUG
		printf("slt --> 0x%x < 0x%x ? %x\n", opnd1, opnd2, result);
#endif // DEBUG
		break;
	default:
		perror("Error in ALU calculation");
		result = 0;
		break;
	}
	return result;
}

//control unit
int set_control_signal(int op, int funct) {
	char* name = NULL;
	char temp;
	int temp2 = 0;
	switch (op)
	{
	case 0x0:
		temp = 'R';
		switch (funct)
		{
		case 0x20:
			name = "add";
			temp2 = 0x142;
			break;
		case 0x21:
			name = "addu";
			temp2 = 0x142;
			break;
		case 0x24:
			name = "and";
			temp2 = 0x102;
			break;
		case 0x08:
			name = "jr"; //special
			temp2 = 0xE00;
			break;
		case 0x25:
			name = "or";
			temp2 = 0x122;
			break;
		case 0x27:
			name = "nor";
			temp2 = 0x122; //negative
			break;
		case 0x2a:
			name = "slt";
			temp2 = 0x1E2;
			break;
		case 0x2b:
			name = "sltu";
			temp2 = 0x1E2;
			break;
		case 0x00:
			name = "sll";
			temp2 = 0x182; //shift
			break;
		case 0x02:
			name = "srl";
			temp2 = 0x1A2; //shift
			break;
		case 0x22:
			name = "sub";
			temp2 = 0x1C2;
			break;
		case 0x23:
			name = "subu";
			temp2 = 0x1C2;
			break;
		default:
			break;
		}
		temp = 'R';
		break;
	case 0x2:
		name = "j";
		temp = 'J';
		temp2 = 0xA10;
		break;
	case 0x3:
		name = "jal";
		temp2 = 0x142;
		temp = 'J';
		break;
	case 0x4:
		name = "beq";
		temp2 = 0x2C0;
		temp = 'I';
		break;
	case 0x5:
		name = "bne";
		temp2 = 0x12C0; //negative
		temp = 'I';
		break;
	case 0x8:
		name = "addi";
		temp2 = 0x52;
		temp = 'I';
		break;
	case 0x9:
		name = "addiu";
		temp2 = 0x52;
		temp = 'I';
		break;
	case 0xa:
		name = "slti";
		temp2 = 0xF2;
		temp = 'I';
		break;
	case 0xb:
		name = "sltiu";
		temp2 = 0xF2;
		temp = 'I';
		break;
	case 0xc:
		name = "andi";
		temp2 = 0x12;
		temp = 'I';
		break;
	case 0x23:
		name = "lw";
		temp2 = 0x5B;
		temp = 'I';
		if (instr_info.enable) {
			instr_info.mem_instr_count++;
		}
		break;
	case 0x24:
		name = "lbu"; //skip
		break;
	case 0x25:
		name = "lhu"; //skip
		break;
	case 0x30:
		name = "ll";
		temp2 = 0x5B;
		temp = 'I';
		break;
	case 0xf:
		name = "lui";
		temp2 = 0x52;
		temp = 'I';
		break;
	case 0xd:
		name = "ori";
		temp2 = 0x32;
		temp = 'I';
		break;
	case 0x28:
		name = "sb"; //skip
		break;
	case 0x29:
		name = "sh"; //skip
		break;
	case 0x38:
		name = "sc"; //skip
		break;
	case 0x2b:
		name = "sw";
		temp2 = 0x54;
		temp = 'I';
		if (instr_info.enable) {
			instr_info.mem_instr_count++;
		}
		break;
	default:
		check((-1), "opcode does not exist");
		break;
	}

	if (instr_info.enable) {
		if (temp == 'I') {
			instr_info.i_type_count++;
		}
		else if (temp == 'R') {
			instr_info.r_type_count++;
		}
		else if (temp == 'J') {
			instr_info.j_type_count++;
		}
		else {
			return -1;
		}
	}
#ifdef DEBUG
	printf("Operation: %s\n", name);
#endif // DEBUG
	return temp2;
}

//data hazard detection unit
void hazard_det_unit() {
	int rsD = bit(if_id_l.instruction, 5, 22);
	int rtD = bit(if_id_l.instruction, 5, 17);
	int rtE = id_ex_l.rt;
	int rsE = id_ex_l.rs;
	int WriteRegM = ex_mem_l.write_reg;
	int WriteRegW = mem_wb_l.write_reg;
	int MemToRegM = bit(ex_mem_l.control_unit_wb, 1, 1);
	int MemReadM = bit(ex_mem_l.control_unit_m, 1, 2);

	//data forwarding
	if (WriteRegM != 0 && rsE == WriteRegM && hu.RegWriteM) { hu.ForwardAE = 0x2; }
	else if (WriteRegW != 0 && rsE == WriteRegW && hu.RegWriteW) { hu.ForwardAE = 0x1; }
	else { hu.ForwardAE = 0x0; }

	if (WriteRegM != 0 && rtE == WriteRegM && hu.RegWriteM) hu.ForwardBE = 0x2;
	else if (WriteRegW != 0 && rtE == WriteRegW && hu.RegWriteW) hu.ForwardBE = 0x1;
	else hu.ForwardBE = 0x0;
	
	//branch forwarding
	hu.ForwardAD = (rsD != 0 && rsD == WriteRegM && hu.RegWriteM && !MemReadM) || (hu.BranchD && hu.RegWriteE && (hu.WriteRegE == rsD));
	hu.ForwardBD = (rtD != 0 && rtD == WriteRegM && hu.RegWriteM) || (hu.BranchD && hu.RegWriteE && (hu.WriteRegE == rtD));
}

//stalling detection unit
void stall_unit() {
	int rsD = bit(if_id_l.instruction, 5, 22);
	int rtD = bit(if_id_l.instruction, 5, 17);
	int rtE = id_ex_l.rt;
	int WriteRegM = ex_mem_l.write_reg;
	int MemToRegM = bit(ex_mem_l.control_unit_wb, 1, 1);
	
	int lw_stall = hu.MemtoRegE && ((rsD == rtE) || (rtD == rtE));
	int sw_stall = hu.MemWriteD && (rtD == hu.WriteRegE) && hu.RegWriteE;
	int branch_stall = (hu.BranchD && hu.RegWriteE && (hu.WriteRegE == rsD || hu.WriteRegE == rtD)) ||
		(hu.BranchD && MemToRegM && (WriteRegM == rsD || WriteRegM == rtD));
	
	int stall = lw_stall || branch_stall || sw_stall;
	hu.FlushE = stall;
	hu.StallF = stall;
	hu.StallD = stall;
}

//flush id/ex latch
void flush_E() {
	id_ex_l.control_unit_ex = 0;
	id_ex_l.control_unit_m = 0;
	id_ex_l.control_unit_wb = 0;
	id_ex_l.sign_ext_imm = 0;
	id_ex_l.rs_val = 0;
	id_ex_l.rt_val = 0;
	id_ex_l.rd = 0;
	id_ex_l.rt = 0;
	id_ex_l.rs = 0;
}
