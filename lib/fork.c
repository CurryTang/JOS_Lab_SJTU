// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	//uint32_t pos;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if (!(err & FEC_WR) || !(uvpd[PDX(addr)] & PTE_P) || !((uvpt[PGNUM(addr)] & (PTE_P | PTE_COW)) == (PTE_P | PTE_COW))) {
		panic("Incorrect permission in pgfault\n");
	}
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	addr = (void *)ROUNDDOWN(addr, PGSIZE);
	r = sys_page_alloc(0, PFTEMP, PTE_P | PTE_W | PTE_U);
	if (r < 0){
		panic("Fail to allocate the page\n");
	}
	memcpy(PFTEMP, addr, PGSIZE);
	// LAB 4: Your code here.
	r = sys_page_map(0, PFTEMP, 0, addr, PTE_P | PTE_W |PTE_U);
	if (r < 0){
		panic("fail to map the copy-on-write page\n");
	} 
	r = sys_page_unmap(0, PFTEMP);
	if (r < 0){
		panic("Fail to unmap the old page in COW\n");
	}
	return;
	
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	if ((uvpt[pn] & (PTE_W)) || (uvpt[pn] & PTE_COW)){
	// LAB 4: Your code here.
		r = sys_page_map(0, (void *)(pn * PGSIZE), envid, (void *)(pn * PGSIZE), PTE_P | PTE_U | PTE_COW);
		if (r < 0){
			panic("Error in duppage\n");
		}
		r = sys_page_map(0, (void *)(pn * PGSIZE), 0, (void *)(pn*PGSIZE), PTE_P | PTE_U | PTE_COW);
		if (r < 0){
			panic("Error in duppage\n");
		}
	} else {
		r = sys_page_map(0, (void *)(pn * PGSIZE), envid, (void *)(pn*PGSIZE), PTE_P | PTE_U);
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	extern void _pgfault_upcall (void);
	set_pgfault_handler(pgfault);

	envid_t childenv;
	uintptr_t addr;
	int res;
	childenv = sys_exofork();
	if (childenv < 0){
		panic("Error when creating child env");
	} else if (childenv == 0){
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	for(addr = UTEXT; addr < USTACKTOP;addr += PGSIZE){
		if((uvpd[PDX(addr)] & PTE_P) && ((uvpt[PGNUM(addr)] & (PTE_P | PTE_U)) == (PTE_P | PTE_U))){
			duppage(childenv, PGNUM(addr));
		}
	}
	res = sys_page_alloc(childenv, (void *)(UXSTACKTOP - PGSIZE), PTE_U | PTE_W | PTE_P);
	if(res < 0){
		panic("Error to allocate exception stack\n");
	}
	res = sys_env_set_pgfault_upcall(childenv, _pgfault_upcall);
	if(res < 0){
		panic("Error in setting pgfault upcall\n");
	}
	res = sys_env_set_status(childenv, ENV_RUNNABLE);
	if(res < 0){
		panic("Error in setting child process state");
	}
	return childenv;

}


envid_t
priority_fork(int pr)
{
	// LAB 4: Your code here.
	extern void _pgfault_upcall (void);
	set_pgfault_handler(pgfault);

	envid_t childenv;
	uintptr_t addr;
	int res;
	childenv = sys_exofork();
	if (childenv < 0){
		panic("Error when creating child env");
	} else if (childenv == 0){
		thisenv = &envs[ENVX(sys_getenvid())];
		sys_change_priority(pr);
		return 0;
	}
	for(addr = UTEXT; addr < USTACKTOP;addr += PGSIZE){
		if((uvpd[PDX(addr)] & PTE_P) && ((uvpt[PGNUM(addr)] & (PTE_P | PTE_U)) == (PTE_P | PTE_U))){
			duppage(childenv, PGNUM(addr));
		}
	}
	res = sys_page_alloc(childenv, (void *)(UXSTACKTOP - PGSIZE), PTE_U | PTE_W | PTE_P);
	if(res < 0){
		panic("Error to allocate exception stack\n");
	}
	res = sys_env_set_pgfault_upcall(childenv, _pgfault_upcall);
	if(res < 0){
		panic("Error in setting pgfault upcall\n");
	}
	res = sys_env_set_status(childenv, ENV_RUNNABLE);
	if(res < 0){
		panic("Error in setting child process state");
	}
	return childenv;

}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
