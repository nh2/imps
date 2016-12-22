/* Share of work:
 * - Andrejs wrote a version of the emulator on his own.
 * - Rudy and Niklas wrote a version of the emulator together in two-people-one-screen style.
 * - In the end we compared and merged the two versions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

// Size of memory, fixed by spec
#define MEM_SIZE 65536

// Debug chosen to be constant instead of macro so that it is changable on gdb debugging.
const int DEBUG = 1;

// SEMANTIC PRINTFS

#define DEBUG(x) if (DEBUG) { x; }
#define LOG_ERROR(...) fprintf(stderr, __VA_ARGS__)
#define LOG_DEBUG(...) DEBUG( printf(__VA_ARGS__); )

// Opcodes

enum {
	HALT,
	ADD,
	ADDI,
	SUB,
	SUBI,
	MUL,
	MULI,
	LW,
	SW,
	BEQ,
	BNE,
	BLT,
	BGT,
	BLE,
	BGE,
	JMP,
	JR,
	JAL
};

// Allows to get 32-bit word from any byte address
#define W32(arr, pos) (*((unsigned int *) &(arr)[pos]))

// signed for arithmethic operations
#define SIGNED(i) ((signed int) i)

// sign extension for immediate parts
#define SIGNEXT(i) ( ((i) & 0b00000000000000001000000000000000 ) ? ((i) | 0b11111111111111110000000000000000) : (i) )

/* Prints the 32-bit instruction bit by bit */
void print_instruction (unsigned int instruction)
{
	unsigned int mask = 1 << 30;
	while (mask) { putchar(instruction & mask ? '1' : '0'); mask >>= 1; }
}

/* Read the file at filename into the buffer, limiting the read to limit bytes.
 * Returns the number of bytes read.
 */
size_t read_binary_file_into_buffer(char *filename, unsigned char *buf, size_t limit)
{
	size_t bytes_read;
	// rb for read, binary
	FILE *file = fopen(filename, "rb");

	if (!file) {
		LOG_ERROR("Error opening file %s\n", filename);
		return 1;
	}

	bytes_read = fread(buf, 1, limit, file);
	// Make sure to not leak file handle
	fclose(file);
	return bytes_read;
}

/* Returns 1 if the given address is in memory range, 0 otherwise. */
int in_memory_bounds (unsigned int addr) {
	return addr < MEM_SIZE;
}

/* We chose to not encapsulate the state of the "virtual machine"
 * in a struct as it makes the program easier to read, has (at least)
 * the same performance and is not necessary for this task.
 */

/* Prints the state of of the virtual machine, namely PC and all register contents */
void print_state(unsigned int PC, int *registers) {
	int i;
	
	printf("\n");
	printf("Registers:\n");
	printf("PC : %10d (0x%.8x)\n", PC, PC);
		for (i = 0; i < 32; i++) {
			printf("$%-2d: %10d (0x%.8x)\n", i, registers[i], registers[i]);
		}
}

int main (int argc, char *argv[])
{
	if (argc != 2) {
		LOG_ERROR("imps-emulator takes exactly one argument\n");
		return 1;
	}

	// The memory of the emulator, fixed to 16 bit byte-addressable space
	unsigned char memory[MEM_SIZE] = {0};
	// program size in bytes
	unsigned int program_size = 0;

	// The 32 general-purpose registers of the emulator, each 32 bit
	// signed int such that arithmetic expressions are simple to implement
	int registers[32] = {0};

	/* NOTE: Register count 32 is not macro'd because it determines
	 * all instructions.
	 */

	// program counter
	unsigned int PC = 0;

	// Read in program

	char *program_filename = argv[1];

	program_size = read_binary_file_into_buffer(program_filename, memory, MEM_SIZE);

	LOG_DEBUG("read %d bytes from program file\n", program_size);

	/* Main computation loop.
	 * Runs a fetch-execute-cycle until HALT is encountered or an error is encountered,
	 * e.g. an unknown instruction is to be executed or memory borders are exceeded.
	 */

	while (1) {
		LOG_DEBUG("PC: %d\t- ", PC);

		// fetch

		unsigned int instruction = W32(memory, PC);

		// Instruction part access
		
		/* These macros are fixed to the instruction being called "instruction"
		 * (therefore we declared them locally) so that we can write OPCODE
		 * instead of OPCODE(instruction), which makes the program a lot cleaner.
		 */

		#define OPCODE ( (instruction & 0b11111100000000000000000000000000) >> 26 )
		#define R1     ( (instruction & 0b00000011111000000000000000000000) >> 21 )
		#define R2     ( (instruction & 0b00000000000111110000000000000000) >> 16 )
		#define R3     ( (instruction & 0b00000000000000001111100000000000) >> 11 )
		#define IMM    ( (instruction & 0b00000000000000001111111111111111) >>  0 )
		#define ADDR   ( (instruction & 0b00000011111111111111111111111111) >>  0 )

		/* REMEMBER that R1 is not register 1, but the R1 part of the instruction
		 * as in the spec. The actual registers are accessed with register[i].
		 */

		// execute

		DEBUG(print_instruction(instruction));
		LOG_DEBUG(" ");

		/* We chose a simple switch instead of a virtual function table as this is
		 * an easy-to-understand, easily debuggable and clean solution and also
		 * allows control over the outer while loop (e.g. for when PC incrementation)
		 * shall be skipped.
		 * We have enough faith in gcc to believe that it optimises it to be at least
		 * as fast as a vtable.
		 */

		/* REMEMBER that for immediate instructions, the IMM part has to be
		 * treated as signed and also sign-extended.
		 */

		switch (OPCODE) {
		
			case HALT:
				LOG_DEBUG("HALT\n");
				// The spec do not say this, but the provied result files increment the PC after HALT
				PC += 4;
				print_state(PC, registers);
				return 0;
			
			// Arithmetics
			
			case ADD:
				LOG_DEBUG("ADD R%d = R%d + R%d\n", R1, R2, R3);
				registers[R1] = registers[R2] + registers[R3];
				break;

			case ADDI:
				LOG_DEBUG("ADDI R%d = R%d + %d\n", R1, R2, SIGNEXT(SIGNED(IMM)));
				registers[R1] = registers[R2] + SIGNEXT(SIGNED(IMM));
				break;

			case SUB:
				LOG_DEBUG("SUB R%d = R%d - R%d\n", R1, R2, R3);
				registers[R1] = registers[R2] - SIGNED(registers[R3]);
				break;

			case SUBI:
				LOG_DEBUG("SUBI R%d = R%d - %d\n", R1, R2, SIGNEXT(SIGNED(IMM)));
				registers[R1] = registers[R2] - SIGNEXT(SIGNED(IMM));
				break;

			case MUL:
				LOG_DEBUG("MUL R%d = R%d - R%d\n", R1, R2, R3);
				registers[R1] = registers[R2] * SIGNED(registers[R3]);
				break;

			case MULI:
				LOG_DEBUG("MULI R%d = R%d + %d\n", R1, R2, SIGNEXT(SIGNED(IMM)));
				registers[R1] = registers[R2] * SIGNEXT(SIGNED(IMM));
				break;

			// Load and store

			case LW:
				LOG_DEBUG("LW R%d = MEMORY[R%d + %d]\n", R1, R2, SIGNEXT(SIGNED(IMM)));
				// Scope this because switch allows re-use of variables in different cases.
				{
					unsigned int addr = registers[R2] + SIGNEXT(SIGNED(IMM));
					// Check memory violation according to the spec
					if (!in_memory_bounds(addr)) {
						LOG_DEBUG("Load access from address %d out of allowed range\n", addr);
						DEBUG(print_instruction(instruction));
						LOG_ERROR("\n");
						return 1;
					}
					registers[R1] = W32(memory, addr);
				}
				break;

			case SW:
				LOG_DEBUG("SW MEMORY[R%d + %d] = %d\n", R1, R2, registers[R1]);
				{
					unsigned int addr = registers[R2] + SIGNEXT(SIGNED(IMM));
					if (!in_memory_bounds(addr)) {
						LOG_DEBUG("Store access to address %d out of allowed range\n", addr);
						DEBUG(print_instruction(instruction));
						LOG_ERROR("\n");
						return 1;
					}
					W32(memory, addr) = registers[R1];
				}
				break;

			// Branching

			case BEQ:
				LOG_DEBUG("BEQ if R%d == R%d then PC = PC + (%d * 4)\n", R1, R2, SIGNEXT(SIGNED(IMM)));
				if (registers[R1] == registers[R2]) {
					PC += SIGNEXT(SIGNED(IMM)) * 4;
					continue;
				}
				break;

			case BNE:
				LOG_DEBUG("BNE if R%d != R%d then PC = PC + (%d * 4)\n", R1, R2, SIGNEXT(SIGNED(IMM)));
				if (registers[R1] != registers[R2]) {
					PC += SIGNEXT(SIGNED(IMM)) * 4;
					continue;
				}
				break;

			case BLT:
				LOG_DEBUG("BLT if R%d < R%d then PC = PC + (%d * 4)\n", R1, R2, SIGNEXT(SIGNED(IMM)));
				// NOTE: This relies on the fact that registers[] is signed int.
				if (registers[R1] < registers[R2]) {
					PC += SIGNEXT(SIGNED(IMM)) * 4;
					continue;
				}
				break;

			case BGT:
				LOG_DEBUG("BGT if R%d > R%d then PC = PC + (%d * 4)\n", R1, R2, SIGNEXT(SIGNED(IMM)));
				if (registers[R1] > registers[R2]) {
					PC += SIGNEXT(SIGNED(IMM)) * 4;
					continue;
				}
				break;

			case BLE:
				LOG_DEBUG("BLE if R%d <= R%d then PC = PC + (%d * 4)\n", R1, R2, SIGNEXT(SIGNED(IMM)));
				if (registers[R1] <= registers[R2]) {
					PC += SIGNEXT(SIGNED(IMM)) * 4;
					continue;
				}
				break;

			case BGE:
				LOG_DEBUG("BGE if R%d >= R%d then PC = PC + (%d * 4)\n", R1, R2, SIGNEXT(SIGNED(IMM)));
				if (registers[R1] >= registers[R2]) {
					PC += SIGNEXT(SIGNED(IMM)) * 4;
					continue;
				}
				break;

			// Jumping

			case JMP:
				LOG_DEBUG("JMP PC = %d\n", ADDR);
				PC = ADDR;
				continue;

			case JR:
				LOG_DEBUG("JR PC = R%d\n", R1);
				PC = registers[R1];
				continue;

			case JAL:
				LOG_DEBUG("JAL R31 = PC + 4; PC = %d\n", ADDR);
				registers[31] = PC + 4;
				PC = ADDR;
				continue;

			default:
				LOG_DEBUG("UNKNOWN INSTRUCTION ");
				DEBUG(print_instruction(instruction));
				LOG_DEBUG("\n");
		}
				
		/* for BRANCHES and JUMPS, this MUST NOT BE EXECUTED
		 * (therefore, continue is used),
		 * because otherwise we skip an instruction
		 */
		// increment PC
		PC += 4;
	}
}

