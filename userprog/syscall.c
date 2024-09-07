#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/************************************************************************/
/* syscall, project 2  */

typedef void syscall_handler_func(struct intr_frame *);
static syscall_handler_func *syscall_handlers[25]; // 25는 총 syscall 갯수;

static void  
write_handler(struct intr_frame* f);

static void 
wait_handler(struct intr_frame* f);

static void 
exit_handler(struct intr_frame* f);

/* syscall, project 2 */
/************************************************************************/

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	

	syscall_handlers[SYS_WAIT] = wait_handler;
	syscall_handlers[SYS_WRITE] = write_handler; 
	syscall_handlers[SYS_EXIT] = exit_handler;
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

	syscall_handler_func *handler;

	int sys_num = f->R.rax;
	handler = syscall_handlers[sys_num];
	handler(f);

	//printf ("system call!\n");
	//thread_exit ();
}



static void  
write_handler(struct intr_frame* f)
{	
	int fd;
	void* buffer;
	unsigned size;

	fd = f->R.rdi;
	buffer = f->R.rsi;
	size = f->R.rdx;

	if(fd == STDOUT_FILENO)	
		putbuf(buffer,size);
}

static void 
wait_handler(struct intr_frame* f)
{
	int pid;

	pid = f->R.rdi;
	//pid가 유효한지 체크

	
}


static void 
exit_handler(struct intr_frame* f)
{
	int status;
	status = f->R.rdi;
	f->R.rax = status;
	thread_exit();
}