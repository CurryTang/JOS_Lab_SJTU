// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "time", "Record the time of a command", time},
	{ "showmappings", "Display the VA&PA maping", showmappings},
	{ "perm", "Change the permession bit", perm},
	{ "dump", "dump the content", dump}
};

/***** Implementations of basic kernel monitor commands *****/

static int
my_strtol(char *str)
{
	int ptr;
    int temp, i; 
    size_t length;

    length = strlen(str) ;
    ptr = 0; 
    for (i = 2; str[i] != 0; i++) {

        if(str[i] <= '9') 
            temp = str[i] - '0';
        else
            temp = str[i] - 'a' + 10;
        ptr += temp * ( 1 << (length - i - 1) * 4 );
    }
    return ptr;
}


int showmappings(int argc, char **argv, struct Trapframe *tf){
	extern pte_t *pgdir_walk(pde_t *pgdir, const void *va, int create);
    extern pde_t *kern_pgdir;
	if (argc != 3){
		cprintf("Correct usage: showmapping <address 1> <address 2>\n");
		return 0;
	}

	if (argv[1][0] != '0' || argv[1][1] != 'x' || argv[2][0] != '0' || argv[2][1] != 'x'){
		cprintf("Error format\n");
		return 0;
	}
	int va_start_num = my_strtol(argv[1]);
	int va_end_num = my_strtol(argv[2]);
	if (va_start_num > va_end_num || va_end_num > 0xffffffff){
		cprintf("Invalid range\n");
		return 0;
	}
	for(;va_start_num <= va_end_num;va_start_num += PGSIZE){
		cprintf("%08x: ", va_start_num);
		pte_t *pte = pgdir_walk(kern_pgdir, (void*)va_start_num, 0);
        if (!pte)
        {
            cprintf("not mapped\n");
			continue;
        }
		cprintf("%08x", PTE_ADDR(*pte));
		cprintf("PTE_P:%d PTE_W:%d PTE_U:%d\n", *pte&PTE_P, *pte&PTE_W, *pte&PTE_U);
	}
	return 0;
}

int perm(int argc, char **argv, struct Trapframe *tf){
	extern pte_t *pgdir_walk(pde_t *pgdir, const void *va, int create);
    extern pde_t *kern_pgdir;
	if (argc != 4){
		cprintf("Correct usage: perm <address> <bit(P/U/W)> <yes/no(1/0)>\n");
		return 0;
	}

	int address = my_strtol(argv[1]);
	char *bit = argv[2];
	int yes_or_no = my_strtol(argv[3]);

	pte_t *pte = pgdir_walk(kern_pgdir, (void *)address, 0);
	if(!pte){
		cprintf("No mapping entry at this VA\n");
		return 0;
	}
	if (yes_or_no){
		switch(bit[0]){
			case 'P':{
				*pte |= PTE_P;
				break;
			}
			case 'W': {
				*pte |= PTE_W;
				break;
			} 
			case 'U': {
				*pte |= PTE_U;
				break;
			}
		}
	} else {
		switch(bit[0]){
			case 'P':{
				*pte &= (~PTE_P);
				break;
			}
			case 'W': {
				*pte &= (~PTE_W);
				break;
			} 
			case 'U': {
				*pte &= (~PTE_U);
				break;
			}
		}
	}
	return 0;
}


int dump(int argc, char **argv, struct Trapframe *tf){
	extern pte_t *pgdir_walk(pde_t *pgdir, const void *va, int create);
    extern pde_t *kern_pgdir;
	if (argc != 4){
		cprintf("Correct usage: dump <PA/VA> <address 1> <address 2>\n");
		return 0;
	}

	if (argv[2][0] != '0' || argv[2][1] != 'x' || argv[3][0] != '0' || argv[3][1] != 'x'){
		cprintf("Invalid number argument\n");
		return 0;
	}

	int start_addr = my_strtol(argv[2]);
	int end_addr = my_strtol(argv[3]);
	if (argv[1][0] == 'P'){
		start_addr = (int)KADDR((uint32_t)start_addr);
		end_addr = (int)KADDR((uint32_t)end_addr);
	}
	for(;start_addr <= end_addr;start_addr++){
		cprintf("%08x: %08x\n", start_addr, *((char *)start_addr));
	}
	return 0;
	
}

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

uint64_t rdtsc() {
	uint32_t lo, hi;
	__asm __volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return (uint64_t)hi << 32 | lo;
}

// time command
int time(int argc, char **argv, struct Trapframe *tf){
	if (argc <= 1){
		cprintf("Correct Usage: time [command] \n");
	}

	int i = 0;
	for (; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[1], commands[i].name) == 0)
			break;
		if (i == ARRAY_SIZE(commands) - 1){
			cprintf("Error, unknown command\n");
			return 0;
		}
	}

	uint64_t begin = rdtsc();
	commands[i].func(argc - 1, argv + 1, tf);
	uint64_t end = rdtsc();

	cprintf("%s cycles: %llu\n", argv[1], end - begin);
	return 0;
}


// Lab1 only
// read the pointer to the retaddr on the stack
static uint32_t
read_pretaddr() {
    uint32_t pretaddr;
    __asm __volatile("leal 4(%%ebp), %0" : "=r" (pretaddr)); 
    return pretaddr;
}

void
do_overflow(void)
{
    cprintf("Overflow success\n");
}



void
start_overflow(void)
{
	// You should use a techique similar to buffer overflow
	// to invoke the do_overflow function and
	// the procedure must return normally.

    // And you must use the "cprintf" function with %n specifier
    // you augmented in the "Exercise 9" to do this job.

    // hint: You can use the read_pretaddr function to retrieve 
    //       the pointer to the function call return address;

	char str[256] = {};
    int nstr = 0;
    char *pret_addr;
	pret_addr = (char*)read_pretaddr(); // get eip pointer
	char *old_addr = *(char **)pret_addr;
	int i = 0;
	for (;i < 256; i++) {
		str[i] = 'h';
	}
	uint32_t ret_addr = (uint32_t)do_overflow;
	uint32_t ret_byte_0 = ret_addr & 0xff; 
	uint32_t ret_byte_1 = (ret_addr >> 8) & 0xff;
	uint32_t ret_byte_2 = (ret_addr >> 16) & 0xff; 
	uint32_t ret_byte_3 = (ret_addr >> 24) & 0xff;
	uint32_t old_byte_0 = (((uint32_t)old_addr)) & 0xff;
	uint32_t old_byte_1 = (((uint32_t)old_addr) >> 8) & 0xff;
	uint32_t old_byte_2 = (((uint32_t)old_addr) >> 16) & 0xff;   
	uint32_t old_byte_3 = (((uint32_t)old_addr) >> 24) & 0xff;  
	str[ret_byte_0] = '\0';
	cprintf("%s%n\n", str, pret_addr);
	str[ret_byte_0] = 'h';
	str[ret_byte_1] = '\0';
	cprintf("%s%n\n", str, pret_addr+1);
	str[ret_byte_1] = 'h';
	str[ret_byte_2] = '\0';
	cprintf("%s%n\n", str, pret_addr+2);
	str[ret_byte_2] = 'h';
	str[ret_byte_3] = '\0';
	cprintf("%s%n\n", str, pret_addr+3);
	str[ret_byte_3] = 'h';
	str[old_byte_0] = '\0';
	cprintf("%s%n\n", str, pret_addr + 4);
	str[old_byte_0] = 'h';
	str[old_byte_1] = '\0';
	cprintf("%s%n\n", str, pret_addr + 5);
	str[old_byte_1] = 'h';
	str[old_byte_2] = '\0';
	cprintf("%s%n\n", str, pret_addr + 6);
	str[old_byte_2] = 'h';
	str[old_byte_3] = '\0';
	cprintf("%s%n\n", str, pret_addr + 7);
	


}

void
overflow_me(void)
{
    start_overflow();
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t ebp = read_ebp();
	cprintf("Stack backtrace:\n");
	// when ebp == 0, we go back to the beginning
	while(ebp != 0x0){
		uint32_t* eip = (uint32_t *)(ebp) + 1;
		cprintf("  eip %08x  ebp %08x  ", *(eip), ebp);
		cprintf("args %08x %08x %08x %08x %08x\n", *(eip + 1), *(eip + 2), *(eip + 3), *(eip + 4), *(eip + 5));
		struct Eipdebuginfo eii;
		debuginfo_eip(*eip, &eii);
		cprintf("	 %s:%d ", eii.eip_file, eii.eip_line);
		for(int i = 0;i < eii.eip_fn_namelen;i++){
			cputchar(eii.eip_fn_name[i]);
		}
		cprintf("%+d\n", *eip - eii.eip_fn_addr);
		ebp = *((uint32_t*)ebp);
	}
	overflow_me();
    cprintf("Backtrace success\n");
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}