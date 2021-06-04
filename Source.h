#include <time.h>
#define MAX_MEM 16*1024*1024/sizeof(int)

//global variable
//unsigned int PC, IR;
unsigned int reg[32];
unsigned int data_memory[MAX_MEM];
unsigned int instr_memory[MAX_MEM];

unsigned int k, clk;
clock_t temp[MAX_MEM], start_t, end_t, clk_t;

struct instr_info {
	int i_type_count, r_type_count, j_type_count, mem_instr_count, 
		branch, branch_taken, nop, enable;
};
typedef struct instr_info info;
info instr_info;

struct if_id_latch {
	int pc;
	int instruction;
	int i;
};
typedef struct if_id_latch if_id_latch;
if_id_latch if_id_l;

struct id_ex_latch {
	int rs_val, rt_val, rs, rt, rd, shamt, sign_ext_imm;
	int control_unit_wb, control_unit_m, control_unit_ex;
	int i;
};
typedef struct id_ex_latch id_ex_latch;
id_ex_latch id_ex_l;

struct ex_mem_latch {
	int rt, alu_out, write_reg;
	int control_unit_wb, control_unit_m;
	int i;
};
typedef struct ex_mem_latch ex_mem_latch;
ex_mem_latch ex_mem_l;

struct mem_wb_latch {
	int alu_out, read_data, write_reg;
	int control_unit_wb;
	int i;
};
typedef struct mem_wb_latch mem_wb_latch;
mem_wb_latch mem_wb_l;

struct hazardUnit {
	//signal produced by the hazard unit
	int StallF, StallD, FlushE,
		ForwardAD, ForwardBD, ForwardAE, ForwardBE;

	//data saved to produce signal
	int	BranchD, MemtoRegE, WriteRegE, MemWriteD,
		RegWriteE, RegWriteM, RegWriteW, instrD;
};
typedef struct hazardUnit haz_unit;
haz_unit hu;

void load_instruction(FILE* fd);
void instruction_fetch(int* PCBranchD, int* PCSrcD);
void instruction_decode(int* ALUOutM, int* PCBranchD, int* PCSrcD);
void execution(int* ALUOutM, int* ResultW);
void memory_access(int* ALUOutM);
void write_back(int* ResultW);

void update_clk_t();
void clock_cycle();
void initialize();

int mux2(int opnd1, int opnd2, int s);
int mux3(int opnd1, int opnd2, int opnd3, int s);
int bit(int number, int numBit, int start);
int swap(int a);
int check_sign_extend(unsigned int imm, int opcode);
int check(int exp, const char* msg);

int print_output();
int ALU_calculator(int opnd1, int opnd2, int E, int control);
int set_control_signal(int op, int funct);
void hazard_det_unit();
void stall_unit();
void flush_E();