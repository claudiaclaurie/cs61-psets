#include "kernel.h"
#include "lib.h"

// kernel.c
//
//    This is the kernel.


// INITIAL PHYSICAL MEMORY LAYOUT
//
//  +-------------- Base Memory --------------+
//  v                                         v
// +-----+--------------------+----------------+--------------------+---------/
// |     | Kernel      Kernel |       :    I/O | App 1        App 1 | App 2
// |     | Code + Data  Stack |  ...  : Memory | Code + Data  Stack | Code ...
// +-----+--------------------+----------------+--------------------+---------/
// 0  0x40000              0x80000 0xA0000 0x100000             0x140000
//                                             ^
//                                             | \___ PROC_SIZE ___/
//                                      PROC_START_ADDR

#define PROC_SIZE 0x40000       // initial state only

static proc processes[NPROC];   // array of process descriptors
                                // Note that `processes[0]` is never used.
proc* current;                  // pointer to currently executing proc

#define HZ 100                  // timer interrupt frequency (interrupts/sec)
static unsigned ticks;          // # timer interrupts so far

void schedule(void);
void run(proc* p) __attribute__((noreturn));


// PAGEINFO
//
//    The pageinfo[] array keeps track of information about each physical page.
//    There is one entry per physical page.
//    `pageinfo[pn]` holds the information for physical page number `pn`.
//    You can get a physical page number from a physical address `pa` using
//    `PAGENUMBER(pa)`. (This also works for page table entries.)
//    To change a physical page number `pn` into a physical address, use
//    `PAGEADDRESS(pn)`.
//
//    pageinfo[pn].refcount is the number of times physical page `pn` is
//      currently referenced. 0 means it's free.
//    pageinfo[pn].owner is a constant indicating who owns the page.
//      PO_KERNEL means the kernel, PO_RESERVED means reserved memory (such
//      as the console), and a number >=0 means that process ID.
//
//    pageinfo_init() sets up the initial pageinfo[] state.

typedef struct physical_pageinfo {
    int8_t owner;
    int8_t refcount;
} physical_pageinfo;

static physical_pageinfo pageinfo[PAGENUMBER(MEMSIZE_PHYSICAL)];

typedef enum pageowner {
    PO_FREE = 0,                // this page is free
    PO_RESERVED = -1,           // this page is reserved memory
    PO_KERNEL = -2              // this page is used by the kernel
} pageowner_t;

static void pageinfo_init(void);


// Memory functions

void virtual_memory_check(void);
void memshow_physical(void);
void memshow_virtual(x86_pagetable* pagetable, const char* name);
void memshow_virtual_animate(void);


// kernel(command)
//    Initialize the hardware and processes and start running. The `command`
//    string is an optional string passed from the boot loader.

static void process_setup(pid_t pid, int program_number);

void kernel(const char* command) {
    hardware_init();
    pageinfo_init();
    console_clear();
    timer_init(HZ);

    // Set up process descriptors
    memset(processes, 0, sizeof(processes));
    for (pid_t i = 0; i < NPROC; i++) {
        processes[i].p_pid = i;
        processes[i].p_state = P_FREE;
    }
    
    // Map memory below 0x100000 to be inaccessible to applications
    virtual_memory_map(kernel_pagetable, 0, 0, PROC_START_ADDR, PTE_P|PTE_W);
    
    // Map CGA console to be accessible to applications
    virtual_memory_map(kernel_pagetable, (uintptr_t) console, (uintptr_t) console, 
                                        PAGESIZE, PTE_P|PTE_W|PTE_U);

    if (command && strcmp(command, "fork") == 0)
        process_setup(1, 4);
    else if (command && strcmp(command, "forkexit") == 0)
        process_setup(1, 5);
    else
        for (pid_t i = 1; i <= 4; ++i)
            process_setup(i, i - 1);

    // Switch to the first process using run()
    run(&processes[1]);
}

// looks through pageinfo array and returns the physical address of
// the first free page, otherwise returns -1
uintptr_t find_free_page(void) {
    for (int i = 0; i < NPAGES; ++i) {
        if (pageinfo[i].owner == PO_FREE)
            return PAGEADDRESS(i);
    }
    return -1;
}

// Finds free phys address below PROC_START_ADDR for pagetables for
// the copy_pagetable fuction per restriction from program_load
uintptr_t find_free_page_table(void) {
    for (int i = 0; i < PROC_START_ADDR / PAGESIZE; ++i) {
        if (pageinfo[i].owner == PO_FREE)
            return PAGEADDRESS(i);
    }
    return -1;
}

// Helper function to copy the pagetable for a new process owner.
x86_pagetable* copy_pagetable(x86_pagetable* pagetable, pid_t owner) {
    uintptr_t L1_addr, L2_addr;
    x86_pagetable* L1_pagetable;
    x86_pagetable* L2_pagetable;
    
    // Find and allocate a new page for the L1 pagetable
    L1_addr = find_free_page_table();
    if (physical_page_alloc(L1_addr, owner) == -1)
        return (x86_pagetable*) -1;
    // Find and allocate a new page for the L2 pagetable    
    L2_addr = find_free_page_table();
    if (physical_page_alloc(L2_addr, owner) == -1)
        return (x86_pagetable*) -1;
    
    // Cast L1 and L2 addresses to x86_pagetables and set tables to 0    
    L1_pagetable = (x86_pagetable*) L1_addr;
    L2_pagetable = (x86_pagetable*) L2_addr;
        
    memset(L1_pagetable->entry, 0, PAGESIZE);
    memset(L2_pagetable->entry, 0, PAGESIZE);
    
    // set first entry of L1_pt to be process's L2_pt with proper permissions
    L1_pagetable->entry[0] = (x86_pageentry_t) L2_pagetable | PTE_P | PTE_W | PTE_U;
    
    // finding the offset for kernel pagetable data that we need to copy in before
    // the process's starting address
    //int offset = PAGENUMBER(PROC_START_ADDR) * sizeof(x86_pageentry_t);
    memcpy(L2_pagetable->entry, (x86_pagetable*) PTE_ADDR(pagetable->entry[0]), offset);

    // Mapping memory below 0x100000 to be inaccessible to applications except...
    virtual_memory_map(L1_pagetable, 0, 0, PROC_START_ADDR, PTE_P|PTE_W);
    
    // Mapping CGA console to be accessible to applications
    virtual_memory_map(L1_pagetable, (uintptr_t) console, (uintptr_t) console, 
                                        PAGESIZE, PTE_P|PTE_W|PTE_U);

    
    return L1_pagetable;
}


// process_setup(pid, program_number)
//    Load application program `program_number` as process number `pid`.
//    This loads the application's code and data into memory, sets its
//    %eip and %esp, gives it a stack page, and marks it as runnable.

void process_setup(pid_t pid, int program_number) {
    process_init(&processes[pid], 0);
    // Copy kernel's pagetable
    processes[pid].p_pagetable = copy_pagetable(kernel_pagetable, pid);
    
    if ((int) processes[pid].p_pagetable == -1)
        return;

    int r = program_load(&processes[pid], program_number);
    assert(r >= 0);
    // set esp to be MEMSIZE_VIRTUAL
    processes[pid].p_registers.reg_esp = MEMSIZE_VIRTUAL;
    // find, allocate phys page for the stack then memory map stack page
    uintptr_t stack_page = find_free_page();
    physical_page_alloc(stack_page, pid);
    virtual_memory_map(processes[pid].p_pagetable, 
                        processes[pid].p_registers.reg_esp - PAGESIZE, 
                        stack_page, PAGESIZE, PTE_P|PTE_W|PTE_U);
    processes[pid].p_state = P_RUNNABLE;
}


// physical_page_alloc(addr, owner)
//    Allocates the page with physical address `addr` to the given owner.
//    Fails if physical page `addr` was already allocated. Returns 0 on
//    success and -1 on failure. Used by the program loader.

int physical_page_alloc(uintptr_t addr, int8_t owner) {
    if ((addr & 0xFFF) != 0
        || addr >= MEMSIZE_PHYSICAL
        || pageinfo[PAGENUMBER(addr)].refcount != 0)
        return -1;
    else {
        pageinfo[PAGENUMBER(addr)].refcount = 1;
        pageinfo[PAGENUMBER(addr)].owner = owner;
        return 0;
    }
}


int sys_exit(pid_t pid) {
    for (int i = 0; i < NPAGES; i++) {
        uintptr_t v_addr = PAGEADDRESS(i);
        vamapping vmap = virtual_memory_lookup(kernel_pagetable, v_addr);
        
        if (pageinfo[vmap.pn].owner == pid) {
            pageinfo[vmap.pn].owner = PO_FREE;
            pageinfo[vmap.pn].refcount = 0;
        }
        if (pageinfo[vmap.pn].refcount > 1 && pageinfo[vmap.pn].owner > 0) {
            pageinfo[vmap.pn].refcount--;
        }
    }
    processes[pid].p_state = P_FREE;
    return 0;
}

// Here is my fork function, factored out from the sys_fork call
pid_t fork(void) {
    // find free process slot
    int slot = -1;
    for (int i = 1; i < NPROC; ++i) {
        if (processes[i].p_state == P_FREE) {
            slot = i;
            break;
        }
    }
        
    if (slot == -1) {
        return -1;
    }
           
    // set proc* child to the new process we've forked, set pid, state and copy the
    // pagetable from the parent process.
    proc* child = &processes[slot];
    child->p_pid = slot;
    child->p_state = P_RUNNABLE;       
    x86_pagetable* child_pagetable = copy_pagetable(current->p_pagetable, child->p_pid);
    if ((int) child_pagetable == -1) {
        current->p_registers.reg_eax = -1;
        sys_exit(child->p_pid);
        return -1;
    }
    child->p_pagetable = child_pagetable;
    
    // Iterate through each page number, starting at PROC_START_ADDR and copy process data
    for (int i = PAGENUMBER(PROC_START_ADDR); i < PAGENUMBER(MEMSIZE_VIRTUAL); ++i) {
        uintptr_t v_addr = PAGEADDRESS(i);   
        vamapping vmap = virtual_memory_lookup(current->p_pagetable, v_addr);
            
        if (vmap.perm != 0)
        {
            // if the page is writeable, allocate a new phys page and memory map virtual
            // address to the new phys page
            if (vmap.perm & PTE_W) {
                uintptr_t new_p_addr = find_free_page();
                if ((int) physical_page_alloc(new_p_addr, child->p_pid) == -1) {
                    current->p_registers.reg_eax = -1;
                    sys_exit(child->p_pid);
                    return -1;
                }
                memcpy((char*) new_p_addr, (char*) vmap.pa, PAGESIZE);
                virtual_memory_map(processes[child->p_pid].p_pagetable, v_addr, new_p_addr, PAGESIZE, vmap.perm);
            } else {
            // if the page is just readable, simply increment the refcount and memory map
            // the virtual address to the phys page
                if (pageinfo[vmap.pn].refcount > 0 && pageinfo[vmap.pn].owner > 0) {
                    ++pageinfo[vmap.pn].refcount;
                    virtual_memory_map(child_pagetable, v_addr, vmap.pa, PAGESIZE, PTE_P | PTE_U);
                }
            }    
        }
    }
    // Copy the parent process's registers except set eax to 0 because the fork system
    // call returns 0 in the child process.
    memcpy(&(child->p_registers), &(current->p_registers), sizeof(x86_registers));
    child->p_registers.reg_eax = 0;
    current->p_registers.reg_eax = child->p_pid;
    
    return child->p_pid;
}


// exception(reg)
//    Exception handler (for interrupts, traps, and faults).
//
//    The register values from exception time are stored in `reg`.
//    The processor responds to an exception by saving application state on
//    the kernel's stack, then jumping to kernel assembly code (in
//    k-exception.S). That code saves more registers on the kernel's stack,
//    then calls exception().
//
//    Note that hardware interrupts are disabled whenever the kernel is running.

void exception(x86_registers* reg) {
    // Copy the saved registers into the `current` process descriptor
    // and always use the kernel's page table.
    current->p_registers = *reg;
    set_pagetable(kernel_pagetable);

    // It can be useful to log events using `log_printf`.
    // Events logged this way are stored in the host's `log.txt` file.
    /*log_printf("proc %d: exception %d\n", current->p_pid, reg->reg_intno);*/

    // Show the current cursor location and memory state
    // (unless this is a kernel fault).
    console_show_cursor(cursorpos);
    if (reg->reg_intno != INT_PAGEFAULT || (reg->reg_err & PFERR_USER)) {
        virtual_memory_check();
        memshow_physical();
        memshow_virtual_animate();
    }

    // If Control-C was typed, exit the virtual machine.
    check_keyboard();


    // Actually handle the exception.
    switch (reg->reg_intno) {

    case INT_SYS_PANIC:
        panic(NULL);
        break;                  // will not be reached

    case INT_SYS_GETPID:
        current->p_registers.reg_eax = current->p_pid;
        break;

    case INT_SYS_YIELD:
        schedule();
        break;                  /* will not be reached */

    case INT_SYS_PAGE_ALLOC: {
        uintptr_t v_addr = current->p_registers.reg_eax;
        uintptr_t p_addr = find_free_page();
        int r = physical_page_alloc(p_addr, current->p_pid);
        if (r >= 0)
            virtual_memory_map(current->p_pagetable, v_addr, p_addr,
                               PAGESIZE, PTE_P|PTE_W|PTE_U);
        else console_printf(CPOS(24, 0), 0X0C00, "Out of physical memory!\n");
        current->p_registers.reg_eax = r;        
        break;
    }

    case INT_TIMER:
        ++ticks;
        schedule();
        break;                  /* will not be reached */
        
    case INT_SYS_FORK: {
        // call fork
        fork();
        break;
    }
    
    case INT_SYS_EXIT: {
        // call sys_exit on current process
        sys_exit(current->p_pid);
        break;
    }

    case INT_PAGEFAULT: {
        // Analyze faulting address and access type.
        uintptr_t addr = rcr2();
        const char* operation = reg->reg_err & PFERR_WRITE
                ? "write" : "read";
        const char* problem = reg->reg_err & PFERR_PRESENT
                ? "protection problem" : "missing page";

        if (!(reg->reg_err & PFERR_USER))
            panic("Kernel page fault for %p (%s %s, eip=%p)!\n",
                  addr, operation, problem, reg->reg_eip);
        console_printf(CPOS(24, 0), 0x0C00,
                       "Process %d page fault for %p (%s %s, eip=%p)!\n",
                       current->p_pid, addr, operation, problem, reg->reg_eip);
        current->p_state = P_BROKEN;
        break;
    }

    default:
        panic("Unexpected exception %d!\n", reg->reg_intno);
        break;                  /* will not be reached */

    }


    // Return to the current process (or run something else).
    if (current->p_state == P_RUNNABLE)
        run(current);
    else
        schedule();
}


// schedule
//    Pick the next process to run and then run it.
//    If there are no runnable processes, spins forever.

void schedule(void) {
    pid_t pid = current->p_pid;
    while (1) {
        pid = (pid + 1) % NPROC;
        if (processes[pid].p_state == P_RUNNABLE)
            run(&processes[pid]);
        // If Control-C was typed, exit the virtual machine.
        check_keyboard();
    }
}


// run(p)
//    Run process `p`. This means reloading all the registers from
//    `p->p_registers` using the `popal`, `popl`, and `iret` instructions.
//
//    As a side effect, sets `current = p`.

void run(proc* p) {
    assert(p->p_state == P_RUNNABLE);
    current = p;

    set_pagetable(p->p_pagetable);
    asm volatile("movl %0,%%esp\n\t"
                 "popal\n\t"
                 "popl %%es\n\t"
                 "popl %%ds\n\t"
                 "addl $8, %%esp\n\t"
                 "iret"
                 :
                 : "g" (&p->p_registers)
                 : "memory");

 spinloop: goto spinloop;       // should never get here
}


// pageinfo_init
//    Initialize the `pageinfo[]` array.

void pageinfo_init(void) {
    extern char end[];

    for (uintptr_t addr = 0; addr < MEMSIZE_PHYSICAL; addr += PAGESIZE) {
        int owner;
        if (physical_memory_isreserved(addr))
            owner = PO_RESERVED;
        else if ((addr >= KERNEL_START_ADDR && addr < (uintptr_t) end)
                 || addr == KERNEL_STACK_TOP - PAGESIZE)
            owner = PO_KERNEL;
        else
            owner = PO_FREE;
        pageinfo[PAGENUMBER(addr)].owner = owner;
        pageinfo[PAGENUMBER(addr)].refcount = (owner != PO_FREE);
    }
}


// virtual_memory_check
//    Check operating system invariants about virtual memory. Panic if any
//    of the invariants are false.

void virtual_memory_check(void) {
    // Process 0 must never be used.
    assert(processes[0].p_state == P_FREE);

    // The kernel page table should be owned by the kernel;
    // its reference count should equal 1, plus the number of processes
    // that don't have their own page tables.
    // Active processes have their own page tables. A process page table
    // should be owned by that process and have reference count 1.
    // All level-2 page tables must have reference count 1.

    // Calculate expected kernel refcount
    int expected_kernel_refcount = 1;
    for (int pid = 0; pid < NPROC; ++pid)
        if (processes[pid].p_state != P_FREE
            && processes[pid].p_pagetable == kernel_pagetable)
            ++expected_kernel_refcount;

    //log_printf("expected_kernel_refcount = %d\n", expected_kernel_refcount);

    for (int pid = -1; pid < NPROC; ++pid) {
        if (pid >= 0 && processes[pid].p_state == P_FREE)
            continue;

        x86_pagetable* pagetable;
        int expected_owner, expected_refcount;
        if (pid < 0 || processes[pid].p_pagetable == kernel_pagetable) {
            pagetable = kernel_pagetable;
            expected_owner = PO_KERNEL;
            expected_refcount = expected_kernel_refcount;
        } else {
            pagetable = processes[pid].p_pagetable;
            expected_owner = pid;
            expected_refcount = 1;
        }
        
        //log_printf("expected_refcount = %d\n", expected_refcount);
        //log_printf("owner: %d, ref_count: %d\n", pageinfo[PAGENUMBER(pagetable)].owner, pageinfo[PAGENUMBER(pagetable)].refcount);
        
        // Check main (level-1) page table
        assert(PTE_ADDR(pagetable) == (uintptr_t) pagetable);
        assert(PAGENUMBER(pagetable) < NPAGES);
        assert(pageinfo[PAGENUMBER(pagetable)].owner == expected_owner);
        assert(pageinfo[PAGENUMBER(pagetable)].refcount == expected_refcount);

        // Check level-2 page tables
        for (int pn = 0; pn < PAGETABLE_NENTRIES; ++pn)
            if (pagetable->entry[pn] & PTE_P) {
                x86_pageentry_t pte = pagetable->entry[pn];
                assert(PAGENUMBER(pte) < NPAGES);
                assert(pageinfo[PAGENUMBER(pte)].owner == expected_owner);
                assert(pageinfo[PAGENUMBER(pte)].refcount == 1);
            }
    }

    // Check that all referenced pages refer to active processes
    for (int pn = 0; pn < PAGENUMBER(MEMSIZE_PHYSICAL); ++pn)
        if (pageinfo[pn].refcount > 0 && pageinfo[pn].owner >= 0)
            assert(processes[pageinfo[pn].owner].p_state != P_FREE);
}


// memshow_physical
//    Draw a picture of physical memory on the CGA console.

static const uint16_t memstate_colors[] = {
    'K' | 0x0D00, 'R' | 0x0700, '.' | 0x0700, '1' | 0x0C00,
    '2' | 0x0A00, '3' | 0x0900, '4' | 0x0E00, '5' | 0x0F00,
    '6' | 0x0C00, '7' | 0x0A00, '8' | 0x0900, '9' | 0x0E00,
    'A' | 0x0F00, 'B' | 0x0C00, 'C' | 0x0A00, 'D' | 0x0900,
    'E' | 0x0E00, 'F' | 0x0F00
};

void memshow_physical(void) {
    console_printf(CPOS(0, 32), 0x0F00, "PHYSICAL MEMORY");
    for (int pn = 0; pn < PAGENUMBER(MEMSIZE_PHYSICAL); ++pn) {
        if (pn % 64 == 0)
            console_printf(CPOS(1 + pn / 64, 3), 0x0F00, "0x%06X ", pn << 12);

        int owner = pageinfo[pn].owner;
        if (pageinfo[pn].refcount == 0)
            owner = PO_FREE;
        uint16_t color = memstate_colors[owner - PO_KERNEL];
        // darker color for shared pages
        if (pageinfo[pn].refcount > 1)
            color &= 0x77FF;

        console[CPOS(1 + pn / 64, 12 + pn % 64)] = color;
    }
}


// memshow_virtual(pagetable, name)
//    Draw a picture of the virtual memory map `pagetable` (named `name`) on
//    the CGA console.

void memshow_virtual(x86_pagetable* pagetable, const char* name) {
    assert((uintptr_t) pagetable == PTE_ADDR(pagetable));

    console_printf(CPOS(10, 26), 0x0F00, "VIRTUAL ADDRESS SPACE FOR %s", name);
    for (uintptr_t va = 0; va < MEMSIZE_VIRTUAL; va += PAGESIZE) {
        vamapping vam = virtual_memory_lookup(pagetable, va);
        uint16_t color;
        if (vam.pn < 0)
            color = ' ';
        else {
            assert(vam.pa < MEMSIZE_PHYSICAL);
            int owner = pageinfo[vam.pn].owner;
            if (pageinfo[vam.pn].refcount == 0)
                owner = PO_FREE;
            color = memstate_colors[owner - PO_KERNEL];
            // reverse video for user-accessible pages
            if (vam.perm & PTE_U)
                color = ((color & 0x0F00) << 4) | ((color & 0xF000) >> 4)
                    | (color & 0x00FF);
            // darker color for shared pages
            if (pageinfo[vam.pn].refcount > 1)
                color &= 0x77FF;
        }
        uint32_t pn = PAGENUMBER(va);
        if (pn % 64 == 0)
            console_printf(CPOS(11 + pn / 64, 3), 0x0F00, "0x%06X ", va);
        console[CPOS(11 + pn / 64, 12 + pn % 64)] = color;
    }
}


// memshow_virtual_animate
//    Draw a picture of process virtual memory maps on the CGA console.
//    Starts with process 1, then switches to a new process every 0.25 sec.

void memshow_virtual_animate(void) {
    static unsigned last_ticks = 0;
    static int showing = 1;

    // switch to a new process every 0.25 sec
    if (last_ticks == 0 || ticks - last_ticks >= HZ / 2) {
        last_ticks = ticks;
        ++showing;
    }

    // the current process may have died -- don't display it if so
    while (showing <= 2*NPROC && processes[showing % NPROC].p_state == P_FREE)
        ++showing;
    showing = showing % NPROC;

    if (processes[showing].p_state != P_FREE) {
        char s[4];
        snprintf(s, 4, "%d ", showing);
        memshow_virtual(processes[showing].p_pagetable, s);
    }
}
