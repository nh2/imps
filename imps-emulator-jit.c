/* COMPILATION NOTE
 * This has to be compiled with `gcc -z execstack` to allow jumping
 * into the stack (which is where the JIT generated code is stored
 * in `jit_area`).
 */

// TODO "i" is a bad variable name

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

#define MEM_SIZE 65536

#ifndef DEBUG_ENABLED
	#define DEBUG_ENABLED 1
#endif

// SEMANTIC PRINTFS

#define DEBUG(x) if (DEBUG_ENABLED) { x; }
#define LOG_ERROR(...) fprintf(stderr, __VA_ARGS__)
#define LOG_DEBUG(...) DEBUG( printf(__VA_ARGS__); )


// Instruction part access

#define OPCODE ( (instruction & 0b11111100000000000000000000000000) >> 26 )
#define R1     ( (instruction & 0b00000011111000000000000000000000) >> 21 )
#define R2     ( (instruction & 0b00000000000111110000000000000000) >> 16 )
#define R3     ( (instruction & 0b00000000000000001111100000000000) >> 11 )
#define IMM    ( (instruction & 0b00000000000000001111111111111111) >>  0 )
#define ADDR   ( (instruction & 0b00000011111111111111111111111111) >>  0 )


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
	JAL,
	JIT,
	LAST_OPCODE
};

char * INSTRUCTION_NAMES[] = {
	"HALT",
	"ADD",
	"ADDI",
	"SUB",
	"SUBI",
	"MUL",
	"MULI",
	"LW",
	"SW",
	"BEQ",
	"BNE",
	"BLT",
	"BGT",
	"BLE",
	"BGE",
	"JMP",
	"JR",
	"JAL",
	"JIT"
};

// Allows to get 32-bit word from any byte address
#define W32(arr, pos) (*((unsigned int *) &(arr)[pos]))

// signed for arithmethic operations
#define SIGNED(i) ((signed int) i)

// sign extension for immediate parts
#define SIGNEXT(i) ( ((i) & 0b00000000000000001000000000000000 ) ? ((i) | 0b11111111111111110000000000000000) : (i) )


void print_instruction_binary (unsigned int instruction)
{
	unsigned int mask = 1 << 30;
	while (mask) { putchar(instruction & mask ? '1' : '0'); mask >>= 1; }
}

size_t read_binary_file_into_buffer(char *filename, unsigned char *buf, size_t limit)
{
	size_t bytes_read;
	FILE *file = fopen(filename, "rb");

	if (!file) {
		LOG_ERROR("Error opening file %s\n", filename);
		return 1;
	}

	bytes_read = fread(buf, 1, limit, file);
	fclose(file);
	return bytes_read;
}

int in_memory_bounds (unsigned int addr) {
	return addr < MEM_SIZE;
}


void print_state(unsigned int PC, int *registers) {
	int i;
	
	printf("\n");
	printf("Registers:\n");
	printf("PC : %10d (0x%.8x)\n", PC, PC);
		for (i = 0; i < 32; i++) {
			printf("$%-2d: %10d (0x%.8x)\n", i, registers[i], registers[i]);
		}
}

int jit_check_translation_range(unsigned int start, unsigned int end, unsigned int program_size)
{
	if (start > end) {
		LOG_ERROR("start address >= end address");
		return 0;
	}
	if (end + 4 > program_size) {
		LOG_ERROR("end address %d reaches out of program size %d", end, program_size);
		return 0;
	}
	if ((start - end) % 4 != 0) {
		LOG_ERROR("the space between start and and address cannot contain multiples of instruction");
		return 0;
	}
	return 1;
}

enum {
	E = 4,
	NE = 5,
	L = 12,
	G = 15,
	LE = 14,
	GE = 13
};

// Returns the opposite of the given Intel x86 condition code
// see http://www.posix.nl/linuxassembly/nasmdochtml/nasmdoca.html#section-A.2.2
int negate_condition_code(int condition_code)
{
	// Make use of the fact that negated values are directly following their counterparts
	return (condition_code % 2 == 0) ? condition_code + 1 : condition_code - 1;
}


void nothing() {}

typedef unsigned int (*pc_returning_fn_ptr)();

// from http://www.posix.nl/linuxassembly/nasmdochtml/nasmdoca.html
/* EFFECTIVE ADDRESS:
 * ModR/M byte | optional SIB byte | optional displacement byte/word/dword
 * ModR/M field:  2  |        3       |  3
 *               mod | spare\register | r/m
 * - spare extends the opcode and determines the register (see REGISTER NOs)
 * - direct register reference without memory access: mod = 3, r/m = register no
 * - mod: length of displacement field: 0 -> no displacement, 1 -> 1 byte, 2 -> 4 bytes
 * - r/m: the register to be added to the displacement
 * - only base reg + displacement -> SIB absent; r/m = base register no
 * - r/m = 4 (would mean ESP) -> SIB present, encodes register combination and scaling
 * SIB field:   2  |    3  |   3
 *            base | index | scale
 * where base = register no, index = register no (or 4 for none), scale scales for index*(2^scale)
 * EXCEPTIONS (important though!):
 * - mod = 0, r/m = 5 -> just uses [displacement32] only
 * - mod = 0, r/m = 4, base = 4 -> just uses [displacement32+index] with no base
 * REGISTER NOs:
 *  0   1   2   3   4   5   6   7
 * EAX ECX EDX EBX ESP EBP ESI EDI
 * /k-type instructions, where k some number:
 *  use effective address with spare set to k
 */

# define MODRM(mod, spare, rm) ((mod << 6) + (spare << 3) + rm)
# define EAX 0
# define EBX 3
# define EBP 5
// Entry for the spare field in ModR/M: does not matter
# define SPARE 0

/* NOTE: I think malloc'ing the generated code area and jumping into it
 * is not possible because of Data Execution Prevention (DEP).
 * Therefore, I allocate the generated code area outside of this
 * function call, in the stack.
 */

void jit_exit ()
{
	exit(0);
}

// TODO start/end are bad names
int jit_translate(int *registers, unsigned char *memory, unsigned int start, unsigned int end, unsigned char *jit_area, int *running_jit_start_instruction_no, int *running_jit_end_instruction_no) {
	unsigned int start_instruction = W32(memory, start);
	LOG_DEBUG("first instruction to be JITed is at %d: ", start);
	DEBUG(print_instruction_binary(start_instruction));
	LOG_DEBUG("\n");
	unsigned int end_instruction = W32(memory, end);
	LOG_DEBUG("last instruction to be JITed is at %d: ", end);
	DEBUG(print_instruction_binary(end_instruction));
	LOG_DEBUG("\n");

	// TODO calculate needed jit area size. Should be simple as we know it from the first pass.

	// jit instruction pointer, used to assemble the instructions part by part
	unsigned char *jip;

	unsigned int instruction_count = ((end - start) / 4) + 1;

	// IMPS instruction byte address to JIT address mapping
	unsigned char * mapping[instruction_count];


	/* Two passes: One counts instructions only and calculates the mapping,
	 * the next one tranlates and adjusts jumps / memory references
	 * according to the mapping.
	 */
	{
		int count_only;
		#define TRANSLATE(x) if (!count_only) {x}
		#define COUNT(x) if (count_only) {x}

		// Print out what instruction is generated at what offset (byte) from jit_area.
		// This is to be prepended to every translation into an Intel instruction so that it can be debugged with gdb's x/i jit_area+(offset).
		// THIS DESCRIBES WHAT HAPPENS.
		// TODO do this for all instructions
		#define JIT_ASM(mnemonic, instruction_type) TRANSLATE ( LOG_DEBUG("  +%d %s\n", (jip - jit_area), (mnemonic" | "instruction_type)); )

		// Writes code that returns the control from the JIT back to the interpreter.
		// It shall return (set eax to) the interpreter PC to continue execution at.
		unsigned char * jit_write_leave (unsigned char * jip, int count_only)
		{
			TRANSLATE ( LOG_DEBUG("    writing a return-from-JIT instruction\n"); )

			// Put the return value (PC outside JIT, where to continue) into eax.
			// If there is no jump out of JIT and we just want to continue where the JIT started, the we use the address pc_to_return_to provided by execute() as last parameter (which is ebp + 8).
			// If there is such a jump, it will have overridden pc_to_return_to by calling jit_change_return_pc().
			//          [... other execute parameters ...]
			// ebp+8 -> pc_to_return_to
			//          [return address to where execute() is called]
			// ebp   -> [ebp from the scope where execute() is called]
			//          [... our local variables ...]
			// TODO check 64 bit: is the offset still 8?
			JIT_ASM (
				"mov eax, [ebp + 8]",
				"MOV reg32,r/m32"
			)  // o32 8B /r
			TRANSLATE ( *jip = 0x8b; )
			jip++;
			TRANSLATE ( *jip = MODRM(2, EAX, EBP); )
			jip++;
			TRANSLATE ( W32(jip, 0) = (int) 8; )
			jip += 4;

			JIT_ASM (
				"mov esp, ebp; pop ebp",
				"LEAVE"
			)  // C9
			TRANSLATE ( *jip = 0xc9; )
			jip++;

			JIT_ASM ( "ret", "RET" )  // c3
			*jip = 0xc3;
			jip++;

			return jip;
		}

		// Called from inside JIT code, changes the pc_to_return_to argument to execute() that is later copied to eax by the code created by jit_write_leave() to form the return code of execute(). For a stack layout see jit_write_leave().
		// If this function is not called, the original pc_to_return_to value set as execute parameter is left as is.
		unsigned char * jit_write_change_return_pc(unsigned char * jip, unsigned int new_return_pc)
		{
			TRANSLATE ( LOG_DEBUG("    writing an instruction that would set the JIT return PC to %d\n", new_return_pc); )

			// TODO check 64 bit: is the offset still 8?
			JIT_ASM (
				"mov [ebp + 8], new_return_pc",
				"MOV r/m32,imm32"
			)  // o32 C7 /0 id
			TRANSLATE ( *jip = 0xc7; )
			jip++;
			TRANSLATE ( *jip = MODRM(2, 0, EBP); )
			jip++;
			TRANSLATE ( W32(jip, 0) = (int) 8; )
			jip += 4;
			TRANSLATE ( W32(jip, 0) = (int) new_return_pc; )
			jip += 4;

			return jip;
		}

		unsigned char * jit_write_jump(unsigned char * jip, int count_only, unsigned int i, unsigned int instruction)
		{
			// If PC + 4*C is in the JIT translation, jump around IN the translation
			// otherwise, leave the JIT execution
			// PC + 4*C can be evaluated AT THE TIME OF TRANSLATION

			// Direct jump imm values contain the address to jump to, so address/4 gives us the instruction
			// Note that target_instruction is the instruction number INSIDE JIT (the n-th jitted instruction)
			int target_instruction = (SIGNEXT(SIGNED(IMM)) - start) / 4;  // (... - start) to get the address INSIDE JIT

			if (SIGNEXT(SIGNED(IMM)) % 4 != 0)
			{
				LOG_ERROR("JUMP ADDRESS %d IS NOT ALIGNED (multiple of 4)\n", SIGNEXT(SIGNED(IMM)));
				return 0;
			}

			// 0 <= target_instruction < instruction_count to be still in jitted code
			int in_jit = 0 <= target_instruction && target_instruction < instruction_count;

			if (target_instruction < 0)
			{
				LOG_ERROR("ATTEMPT TO JUMP TO NEGATIVE MEMORY ADDRESS\n");
				return 0;
			}

			if (in_jit)
			{
				// jump inside jit if the cmp condition is met

				TRANSLATE ( LOG_DEBUG("unconditional in-jit-jump\n"); )

				// note that in the second pass, mapping is already completely filled
				unsigned char * in_jit_addr = mapping[target_instruction];
				unsigned char * addr_after_instruction = jip + 5; // because this instruction has jip++, +=4

				JIT_ASM (
					"jmp in_jit_addr",
					"JMP imm"
				)  // E9 rw/rd
				TRANSLATE ( *jip = 0xe9; )
				jip++;
				TRANSLATE ( W32(jip, 0) = (int) ((int) in_jit_addr - (int) addr_after_instruction); )
				jip += 4;
			}
			else
			{
				// jump out of jit if the cmp condition is met

				// where we have to jump outside the JIT if we have to jump
				unsigned int next_pc = i + SIGNEXT(SIGNED(IMM));

				TRANSLATE ( LOG_DEBUG("    This is an unconditional out-of-jit-jump, assembling a JMP to PC=%d\n", next_pc); )

				// set the right PC (next_pc) into [ebp + 8] (where to jump outside in bytecode)
				jip = jit_write_change_return_pc (jip, next_pc);

				jip = jit_write_leave (jip, count_only);

			}
			return jip;
		}

		unsigned char * jit_write_conditional_jump(unsigned char * jip, int count_only, unsigned int i, unsigned int instruction, unsigned int condition_code)
		{
			// move R1 -> eax, R2 -> ebx
			// If PC + 4*C is in the JIT translation, jump around IN the translation
			// otherwise, leave the JIT execution
			// PC + 4*C can be evaluated AT THE TIME OF TRANSLATION

			// Conditional branch imm values are relative to the instruction (i)
			// Note that target_instruction is the instruction number INSIDE JIT (the n-th jitted instruction)
			int target_instruction = i + SIGNEXT(SIGNED(IMM));
			// 0 <= target_instruction < instruction_count to be still in jitted code
			int in_jit = 0 <= target_instruction && target_instruction < instruction_count;
//			LOG_DEBUG("instruction_count is %d\n", instruction_count);
//			LOG_DEBUG("target_instruction is %d\n", target_instruction);

			if (target_instruction < 0)
			{
				LOG_ERROR("ATTEMPT TO JUMP TO NEGATIVE MEMORY ADDRESS\n");
				return 0;
			}

			/* IDEA:
			 * if in_jit:
			 *   if branch?:
			 *     jmp in_jit_addr
			 * else:
			 *   if branch?:
			 *     PC = start + i*4 + 4*C - 4    // -4 because PC += 4 after JIT done
			 */

			/* We have to compare either way, so first write
			 *   R1 -> eax
			 *   R2 -> ebx
			 *   cmp eax ebx
			 * instructions.
			 */

			JIT_ASM (
				"MOV eax, r1",
				"MOV EAX,memoffs32"
			)  // o32 A1 ow/od
			TRANSLATE ( *jip = 0xa1; )
			jip++;
			TRANSLATE ( W32(jip, 0) = (int) &registers[R1]; )
			jip += 4;

			JIT_ASM (
				"MOV ebx, r2",
				"MOV reg32,r/m32"
			)  // o32 8B /r
			TRANSLATE ( *jip = 0x8b; )
			jip++;
			TRANSLATE ( *jip = MODRM(0, EBX, 5); )
			jip++;
			TRANSLATE ( W32(jip, 0) = (int) &registers[R2]; )
			jip += 4;

			JIT_ASM (
				"CMP eax, ebx",
				"CMP reg32,r/m32"
			)  // o32 3B /r
			TRANSLATE ( *jip = 0x3b; )
			jip++;
			TRANSLATE ( *jip = MODRM(3, EAX, EBX); )
			jip++;

			/* Second, write one of
			 *   jump inside JIT
			 *   jump outside JIT.
			 */

			if (in_jit)
			{
				// jump inside jit if the cmp condition is met

				TRANSLATE ( LOG_DEBUG("conditional in-jit-jump\n"); )

				// note that in the second pass, mapping is already completely filled
				unsigned char * in_jit_addr = mapping[target_instruction];
				unsigned char * addr_after_instruction = jip + 6; // because this instruction has jip++, ++, +=4

				JIT_ASM (
					"Jcc NEAR relative(in_jit_addr)",
					"Jcc 80+cc imm"
				)  // 0F 80+cc imm
				// where cc is the condition code
				// see http://www.posix.nl/linuxassembly/nasmdochtml/nasmdoca.html#section-A.2.2
				TRANSLATE ( *jip = 0x0f; )
				jip++;
				TRANSLATE ( *jip = 0x80 + condition_code; )
				jip++;
				TRANSLATE ( W32(jip, 0) = (int) (in_jit_addr - addr_after_instruction); )
				jip += 4;
			}
			else
			{
				// jump out of jit if the cmp condition is met

				// where we have to jump outside the JIT if we have to jump
				unsigned int next_pc = start + 4 * ( i + SIGNEXT(SIGNED(IMM)) );  // conditional branches are relative, so start + ..., and relative to the current instruction's PC (i)

				TRANSLATE ( LOG_DEBUG("    This is a conditional out-of-jit-jump, assembling a conditional out-of-JIT-return to PC=%d\n", next_pc); )

				/* if the jump condition is NOT met (!condition_code), OMIT (jump over) the out-of-JIT jump written by jit_write_leave:
				 *   jit_write_change_return_pc is jip += 10 bytes long and jit_write_leave is jip += 8 bytes long;
				 *   therefore we have to jump over the next 18 bytes.
				 */
				// TODO modify JIT_ASM so that we can put in which cc it is
				JIT_ASM (
					"Jcc +18",
					"Jcc 70+cc imm8"
				)  // 70+cc imm8
				TRANSLATE ( *jip = 0x70 + negate_condition_code(condition_code); )
				jip++;
				TRANSLATE ( W32(jip, 0) = (int) (10 + 8); )
				jip++;

				// set the right PC (next_pc) into [ebp + 8] (where to jump outside in bytecode)
				jip = jit_write_change_return_pc (jip, next_pc);

				jip = jit_write_leave (jip, count_only);

			}
			return jip;
		}

		for (count_only = 1; count_only >= 0; count_only--)
		{
			COUNT (LOG_DEBUG("pass 1/2: counting instructions to generate mapping\n");)
			TRANSLATE (LOG_DEBUG("pass 2/2: translating\n");)

			// reset jip for each pass
			jip = jit_area;

			JIT_ASM (
				"push ebp; mov ebp, esp",
				"ENTER imm,imm"
			)  // C8 iw ib
			TRANSLATE ( *jip = 0xc8; )
			jip++;
			TRANSLATE ( *jip = 0; )
			jip++;
			TRANSLATE ( *jip = 0; )
			jip++;
			TRANSLATE ( *jip = 0; )
			jip++;

			unsigned int i;
			for (i = 0; i < instruction_count; i++)
			{
				unsigned int instruction_no = start + i * 4;
				unsigned int instruction = W32(memory, instruction_no);

				TRANSLATE (
					LOG_DEBUG("translating instruction no. %d at %d: %s ->\n", i, instruction_no, INSTRUCTION_NAMES[OPCODE]);
				)

				COUNT (
					mapping[i] = jip;
				)

				switch (OPCODE) {
					case ADD:
						// move R2 -> eax, R3 -> ebx, eax + ebx -> eax, eax -> R1
						// TODO improve: R2 -> eax, eax + R3 -> eax, eax -> R1
						// TODO improve: R2 -> eax, R3 + eax -> R3 (using ADD r/m32,reg32)

						JIT_ASM (
							"MOV eax, r2",
							"MOV EAX,memoffs32"
						)  // o32 A1 ow/od
						TRANSLATE ( *jip = 0xa1; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R2]; )
						jip += 4;

						JIT_ASM (
							"MOV ebx, r3",
							"MOV reg32,r/m32"
						)  // o32 8B /r
						TRANSLATE ( *jip = 0x8b; )
						jip++;
						TRANSLATE ( *jip = MODRM(0, EBX, 5); )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R3]; )
						jip += 4;

						JIT_ASM (
							"ADD eax, ebx",
							"ADD r/m32,reg32"
						)  // o32 01 /r
						TRANSLATE ( *jip = 0x01; )
						jip++;
						TRANSLATE ( *jip = MODRM(3, EBX, EAX); )
						jip++;

						JIT_ASM (
							"MOV R1, eax",
							"MOV memoffs32,EAX"
						)  // o32 A3 ow/od
						TRANSLATE ( *jip = 0xa3; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R1]; )
						jip += 4;
						break;

					case ADDI:
						// move R2 -> eax, eax + IMM -> eax, eax -> R1

						JIT_ASM (
							"MOV eax, r2",
							"MOV EAX,memoffs32"
						)  // o32 A1 ow/od
						TRANSLATE ( *jip = 0xa1; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R2]; )
						jip += 4;

						JIT_ASM (
							"ADD eax, IMM",
							"ADD EAX,imm32"
						)  // o32 05 id
						TRANSLATE ( *jip = 0x05; )
						jip++;
						TRANSLATE ( W32(jip, 0) = SIGNEXT(SIGNED(IMM)); )
						jip += 4;

						JIT_ASM (
							"MOV R1, eax",
							"MOV memoffs32,EAX"
						)  // o32 A3 ow/od
						TRANSLATE ( *jip = 0xa3; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R1]; )
						jip += 4;
						break;

					case SUB:
						// move R2 -> eax, R3 -> ebx, eax - ebx -> eax, eax -> R1
						// TODO improve: R2 -> eax, eax - R3 -> eax, eax -> R1
						// TODO improve: R2 -> eax, R3 - eax -> R3 (using SUB r/m32,reg32)

						JIT_ASM (
							"MOV eax, r2",
							"MOV EAX,memoffs32"
						)  // o32 A1 ow/od
						TRANSLATE ( *jip = 0xa1; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R2]; )
						jip += 4;

						JIT_ASM (
							"MOV ebx, r3",
							"MOV reg32,r/m32"
						)  // o32 8B /r
						TRANSLATE ( *jip = 0x8b; )
						jip++;
						TRANSLATE ( *jip = MODRM(0, EBX, 5); )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R3]; )
						jip += 4;

						JIT_ASM (
							"SUB eax, ebx",
							"SUB r/m32,reg32"
						)  // o32 29 /r
						TRANSLATE ( *jip = 0x29; )
						jip++;
						TRANSLATE ( *jip = MODRM(3, EBX, EAX); )
						jip++;

						JIT_ASM (
							"MOV R1, eax",
							"MOV memoffs32,EAX"
						)  // o32 A3 ow/od
						TRANSLATE ( *jip = 0xa3; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R1]; )
						jip += 4;
						break;

					case SUBI:
						// move R2 -> eax, eax - IMM -> eax, eax -> R1

						JIT_ASM (
							"MOV eax, r2",
							"MOV EAX,memoffs32"
						)  // o32 A1 ow/od
						TRANSLATE ( *jip = 0xa1; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R2]; )
						jip += 4;

						JIT_ASM (
							"SUB eax, IMM",
							"SUB EAX,imm32"
						)  // o32 2D id
						TRANSLATE ( *jip = 0x2d; )
						jip++;
						TRANSLATE ( W32(jip, 0) = SIGNEXT(SIGNED(IMM)); )
						jip += 4;

						JIT_ASM (
							"MOV R1, eax",
							"MOV memoffs32,EAX"
						)  // o32 A3 ow/od
						TRANSLATE ( *jip = 0xa3; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R1]; )
						jip += 4;
						break;

					case MUL:
						// move R2 -> eax, eax * R3 -> eax, eax -> R1

						JIT_ASM (
							"MOV eax, r2",
							"MOV EAX,memoffs32"
						)  // o32 A1 ow/od
						TRANSLATE ( *jip = 0xa1; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R2]; )
						jip += 4;

						JIT_ASM (
							"IMUL eax, operand",
							"IMUL r/m32"
						)  // o32 F7 /5  - EDX:EAX = EAX * operand
						TRANSLATE ( *jip = 0xf7; )
						jip++;
						TRANSLATE ( *jip = MODRM(0, 5, 5); )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R3]; )
						jip += 4;

						JIT_ASM (
							"MOV R1, eax",
							"MOV memoffs32,EAX"
						)  // o32 A3 ow/od
						TRANSLATE ( *jip = 0xa3; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R1]; )
						jip += 4;
						break;

					case MULI:
						// move R2 -> eax, eax * IMM -> eax, eax -> R1

						JIT_ASM (
							"MOV eax, r2",
							"MOV EAX,memoffs32"
						)  // o32 A1 ow/od
						TRANSLATE ( *jip = 0xa1; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R2]; )
						jip += 4;

						JIT_ASM (
							"IMUL eax, IMM",
							"IMUL reg32,imm32"
						)  // o32 69 /r id
						TRANSLATE ( *jip = 0x69; )
						jip++;
						TRANSLATE ( *jip = MODRM(3, SPARE, 0); )
						jip++;
						TRANSLATE ( W32(jip, 0) = SIGNEXT(SIGNED(IMM)); )
						jip += 4;

						JIT_ASM (
							"MOV R1, eax",
							"MOV memoffs32,EAX"
						)  // o32 A3 ow/od
						TRANSLATE ( *jip = 0xa3; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R1]; )
						jip += 4;
						break;

					case LW:
						// move R2 -> eax, eax + IMM -> eax, mem[eax] -> eax, eax -> R1
						// TODO improve: find shorter sequence

						JIT_ASM (
							"MOV eax, r2",
							"MOV EAX,memoffs32"
						)  // o32 A1 ow/od
						TRANSLATE ( *jip = 0xa1; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R2]; )
						jip += 4;

						JIT_ASM (
							"ADD eax, IMM",
							"ADD EAX,imm32"
						)  // o32 05 id
						TRANSLATE ( *jip = 0x05; )
						jip++;
						TRANSLATE ( W32(jip, 0) = SIGNEXT(SIGNED(IMM)); )
						jip += 4;

						JIT_ASM (
							"MOV eax, [eax]",
							"MOV reg32,r/m32"
						)  // o32 8B /r
						TRANSLATE ( *jip = 0x8b; )
						jip++;
						TRANSLATE ( W32(jip, 0) = MODRM(2, EAX, EAX); )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) memory; )
						jip += 4;

						JIT_ASM (
							"MOV R1, eax",
							"MOV memoffs32,EAX"
						)  // o32 A3 ow/od
						TRANSLATE ( *jip = 0xa3; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R1]; )
						jip += 4;
						break;

					case SW:
						// move R1 -> eax, R2 -> ebx, eax -> mem[ebx + C]

						JIT_ASM (
							"MOV eax, r1",
							"MOV EAX,memoffs32"
						)  // o32 A1 ow/od
						TRANSLATE ( *jip = 0xa1; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R1]; )
						jip += 4;

						JIT_ASM (
							"MOV ebx, r2",
							"MOV reg32,r/m32"
						)  // o32 8B /r
						TRANSLATE ( *jip = 0x8b; )
						jip++;
						TRANSLATE ( *jip = MODRM(0, EBX, 5); )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[R2]; )
						jip += 4;

						JIT_ASM (
							"MOV mem[R2 + IMM], eax",
							"MOV r/m32,reg32"
						)  //  o32 89 /r
						// where mem[R2 + IMM] = memory + IMM + R2 and memory + IMM is a constant
						TRANSLATE ( *jip = 0x89; )
						jip++;
						TRANSLATE ( W32(jip, 0) = MODRM(2, EAX, EBX); )
						jip++;
						TRANSLATE ( W32(jip, 0) = ((int) memory) + SIGNEXT(SIGNED(IMM)) ; )
						jip += 4;
						break;

					case BEQ:
						jip = jit_write_conditional_jump(jip, count_only, i, instruction, 4);  // E (equals)
						break;

					case BNE:
						jip = jit_write_conditional_jump(jip, count_only, i, instruction, 5);  // NE (not equals)
						break;

					case BLT:
						jip = jit_write_conditional_jump(jip, count_only, i, instruction, 12);  // L (lower)
						break;

					case BGT:
						jip = jit_write_conditional_jump(jip, count_only, i, instruction, 15);  // G (greater)
						break;

					case BLE:
						jip = jit_write_conditional_jump(jip, count_only, i, instruction, 14);  // LE (lower equal)
						break;

					case BGE:
						jip = jit_write_conditional_jump(jip, count_only, i, instruction, 13);  // GE (greater equals)
						break;

					case JMP:
						jip = jit_write_jump(jip, count_only, i, instruction);
						break;

					case JR:
						/* We cannot determine statically whether the jumping destination (in the register) is inside the JITed instructions or not.
						 * We need a runtime JIT bounds check for that.
						 * So we assemble the following:
						 *   if running_jit_start_instruction_no <= [destination in register] <= running_jit_end_instruction_no
						 *     jump to mapping([destination in register])
						 *   else:
						 *     jump out of JIT to [destination in register]
						 */

						// SOTA

						LOG_ERROR("TRANSLATING JAR INSTRUCTION NEEDS RUNTIME JIT CHECK, NOT IMPLEMENTED YET.\n");
						return 1;
						break;

					case JAL:
						/* What we have to do:
						 *   registers[31] = current_PC + 4;
						 *   PC = ADDR;
						 * and current_PC = instruction_no (= start + i * 4).
						 */

						JIT_ASM (
							"mov instruction_no+4, r31",
							"MOV r/m32,imm32"
						)  // o32 C7 /0 id
						TRANSLATE ( *jip = 0xc7; )
						jip++;
						TRANSLATE ( *jip = MODRM(0, 0, 5); )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) &registers[31]; )
						jip += 4;
						TRANSLATE ( W32(jip, 0) = (int) instruction_no + 4; )
						jip += 4;

						// The rest is just a normal jump, and although this instruction is not of type JMP, we can just pass in instruction, because the imm part of JMP and JAL look exactly alike and jit_write_jump only uses the imm part.
						jip = jit_write_jump(jip, count_only, i, instruction);
						break;

					case HALT:
						;
						unsigned char * addr_after_instruction = jip + 5; // because this instruction has jip++, +=4

						JIT_ASM (
							"jmp jit_exit",
							"JMP imm"
						)  // E9 rw/rd
						TRANSLATE ( *jip = 0xe9; )
						jip++;
						TRANSLATE ( W32(jip, 0) = (int) ((int) jit_exit - (int) addr_after_instruction); )
						jip += 4;
						break;

					case JIT:
						LOG_ERROR("ATTEMPT TO TRANSLATE JIT INSTRUCTION. If you say \"JIT\" one more time, I dare you, I double dare you, I will not implement this one!");
						return 1;

					default:
						LOG_ERROR("ATTEMPT TO TRANSLATE UNKNOWN INSTRUCTION %d", OPCODE);
						print_instruction_binary(instruction);
						LOG_ERROR("\n");
						return 1;
				}
			}

			jip = jit_write_leave (jip, count_only);
		}
	}

	// RET
	*jip = 0xc3;
	
	return 1;
}

// for easier debugging
static unsigned int execute (pc_returning_fn_ptr ptr, unsigned int pc_to_return_to)
{
	return ptr(pc_to_return_to);
}

static void check_assertions() {
	// Check if there are as many elements in the instruction enum as in the instruction names array.
	assert(sizeof(INSTRUCTION_NAMES) / sizeof(char*) == LAST_OPCODE);
}

int main (int argc, char *argv[])
{
	check_assertions();

	if (argc != 2) {
		LOG_ERROR("imps-emulator takes exactly one argument\n");
		return 1;
	}

	// The memory of the emulator, fixed to 16 bit byte-addressable space
	unsigned char memory[MEM_SIZE] = {0};
	// program size in bytes
	unsigned int program_size = 0;

	// The 32 general-purpose registers of the emulator, each 32 bit
	int registers[32] = {0};

	// Special registers
	unsigned int PC = 0;

	// Runtime information for code in the JIT about which instruction range has been JITed.
	// While code is interpreted instead of JITed, these are set to -1.
	int running_jit_start_instruction_no = -1;
	int running_jit_end_instruction_no = -1;

	char *program_filename = argv[1];

	program_size = read_binary_file_into_buffer(program_filename, memory, MEM_SIZE);

	LOG_DEBUG("read %d bytes from program file\n", program_size);

	while (1) {
		LOG_DEBUG("PC: %d\t- ", PC);

		// fetch

		unsigned int instruction = W32(memory, PC);
		
		// execute

		DEBUG(print_instruction_binary(instruction));
		LOG_DEBUG(" ");

		switch (OPCODE) {
		
			case HALT:
				LOG_DEBUG("HALT\n");
				// The spec does not say this, but the provied result files increment the PC after HALT
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
				//LOG_DEBUG("- MEMORY[R%d + %d] = MEMORY[%d + %d] = %d\n", R2, SIGNEXT(SIGNED(IMM)), registers[R2], IMM, SIGNED(W32(memory, registers[R2] + SIGNEXT(SIGNED(IMM)))));
				//LOG_DEBUG("-- and this is MEMORY[%d]", registers[R2] + SIGNEXT(SIGNED(IMM)))
				{
					unsigned int addr = registers[R2] + SIGNEXT(SIGNED(IMM));
					if (!in_memory_bounds(addr)) {
						LOG_ERROR("Access to address %d: out of allowed range\n", addr);
						DEBUG(print_instruction_binary(instruction));
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
						LOG_ERROR("Access to address %d: out of allowed range\n", addr);
						DEBUG(print_instruction_binary(instruction));
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
				//LOG_DEBUG("- R%d is %d, R%d is %d\n", R1, registers[R1], R2, registers[R2]);
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

			case JIT:
				/* Layout:
				 *
				 *       [... some code ...]
				 * PC -> JIT init instruction
				 *       jit_instructions_start
				 *       jit_instructions_end
				 *       [... some code ...] ("place a")
				 */
				LOG_DEBUG("JIT: checking for instruction start and end addresses\n");
				if (PC + 8 + 4 > program_size) {
						LOG_ERROR("JIT instruction lacks following start and end addresses\n");	
						return 1;
				}
				
				// TODO add relative start address + length
				
				/* Treat all bytes b code jit_instruction_start <= b <= jit_instruction_end
				 * as 32-bit IMPS instructions and JIT-compile them.
				 */
				unsigned int jit_instructions_start = W32(memory, PC + 4);
				unsigned int jit_instructions_end = W32(memory, PC + 8);
				
				// Check that start and end address are in memory range
				if (!jit_check_translation_range(jit_instructions_start, jit_instructions_end, program_size)) {
					LOG_ERROR(" - JIT start/end (%d/%d) instructions are illegal\n", jit_instructions_start, jit_instructions_end);
					return 1;
				}
				
				#define jit_area_size 100
				unsigned char jit_area[jit_area_size] = {0};
				
				// Translate all those instructions into machine instructions
				int translation_successful = jit_translate(registers, memory, jit_instructions_start, jit_instructions_end, jit_area, &running_jit_start_instruction_no, &running_jit_end_instruction_no);
				
				if (!translation_successful) {
					LOG_ERROR(" JIT TRANLATION UNSUCCESSFUL\n");
					return 1;
				}
				
				running_jit_start_instruction_no = jit_instructions_start;
				running_jit_end_instruction_no = jit_instructions_end;

				// Jump into the generated native instructions
				// The generated code will return here
				// The return value (which is in eax, set by jit_write_leave) tells us where to continue.
				LOG_DEBUG("Jumping into generated code ...\n");
				PC = execute((pc_returning_fn_ptr) jit_area, PC + 12);  // 12 = offset of "place a"
				LOG_DEBUG("... generated code returned, setting PC to %d\n", PC);

				running_jit_start_instruction_no = -1;
				running_jit_end_instruction_no = -1;
				
				continue;

			default:
				LOG_DEBUG("UNKNOWN INSTRUCTION ");
				DEBUG(print_instruction_binary(instruction));
				LOG_DEBUG("\n");
				return 1;
		}
				
		// increment PC
		// for BRANCHES and JUMPS, this MUST NOT BE EXECUTED (use continue),
		// because otherwise we skip an instruction
		PC += 4;
	}
}

