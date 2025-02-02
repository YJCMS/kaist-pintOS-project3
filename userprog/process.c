#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif

#include "threads/malloc.h"


/* single word (4) or double word (8) alignment */
#define ALIGNMENT sizeof(char *)

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);


/***********************************************************************/
/* userprogram, project 2 */
#ifdef USERPROG 

#define ALIGNMENT sizeof(char *)

static void 
setup_argument(struct intr_frame* if_, const char* file_name);
static char*
f_name_to_t_name(const char *file_name, char *t_name);
static bool
find_process_by_tid(struct list_elem* e_, void* aux);

static void 
notice_to_parent(struct process * process, int status);


#endif
/* userprogram, project 2 */
/***********************************************************************/

/* General process initializer for initd and other process. */
static void
process_init (void) {
	
#ifdef USERPROG 
	struct thread *current;
	struct fd_table *fd_table;

	current = thread_current ();
	
	/* make fd_table */
	if((fd_table = palloc_get_page(0)) == NULL){
		/* fd_table 생성 실패시에 부모가 알수있게 값을 변경하고 자신은 종료됨. */
		notice_to_parent(current->process,PROCESS_FAILED);
		thread_current()->exit_code = -1;
		thread_exit();
	}
	init_fd(fd_table);
	current->fd_table = fd_table;
	current->is_process = true;
	
#endif
}

/* General process initializer for initd and other process. */

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;
	
	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	/* set wait_info for process process */
	struct thread* t = thread_current();
	char t_name[16];
	f_name_to_t_name(file_name, t_name);
	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (t_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif
	
	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {

	struct thread *parent;
	struct thread * child;
	struct list_elem * elem;
	struct process *process;
	tid_t pid;

	/* fork 횟수를 제한하는 로직 , 메모리 누수가 존재하여 임시로 작업 */

	parent = thread_current();
	/* thread create 실패하는 경우 */
	if((pid = thread_create (name, PRI_DEFAULT, __do_fork, if_)) == TID_ERROR)
		return TID_ERROR;
	/* 생성한 thread를 list로 접근이 불가한 경우 */
	if((elem = list_find(&parent->child_list,find_process_by_tid,&pid)) == NULL)
		return TID_ERROR;

	process = list_entry(elem,struct process, elem);
	lock_acquire(&process->lock);
	/* 아직 생성중인 경우 */
	if(process->status == PROCESS_YET_INIT){ 
		lock_release(&process->lock);
		sema_down(&process->sema);
	}
	/* 생성이 완료된 경우 */
	else{
		lock_release(&process->lock);
		sema_try_down(&process->sema);
	}

	/* 생성이 실패한 경우 */
	if(process->status == PROCESS_FAILED)
		pid = TID_ERROR;
	return pid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kern_pte(pte))
		return true;
	/* 2. Resolve VA from the parent's page map level 4. */
	if ((parent_page = pml4_get_page(parent->pml4, va)) == NULL) { // 할당된 페이지 해제
    	return false;
	}


	/* 3. TODO: Allocate new PAL_USER page for the process and set result to
	 *    TODO: NEWPAGE. */
	if ((newpage = palloc_get_page(PAL_USER)) == NULL){
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);
	/* 5. Add new page to process's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		palloc_free_page(newpage);
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
/* TODO: parent and process must start with the same physical memory */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *current = thread_current ();
	struct thread *parent = current->process->parent;
	struct process* process;
	bool succ = true;

	/* 	Fork의 인자로 받은 aux를 복사, fork 함수는 실행후 do fork가 완료되기 전까지 잠들기 때문에
		스택 메모리를 참조해도 값을 잃어버리지 않는다는 보장이 있다. */
	memcpy (&if_, (struct intr_frame*)aux, sizeof (struct intr_frame));

	/* Fork 받은 Child는 그 return value가 0이어야한다. */
	if_.R.rax = 0; 

	/* 2. Duplicate PT */
	if ((current->pml4 = pml4_create()) == NULL)
		goto error;
	process_activate (current);

#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent)){
		goto error;
	}
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/
	// Parent inherits file resources (e.g., opened file descriptor) to process
	process_init ();
	struct fd_table *fd_table;

	/* duplicate all opened file */
	fd_table = get_user_fd(current);
	memcpy(fd_table, get_user_fd(parent), sizeof(struct fd_table));
	for(int i = 2; i< FD_MAX_SIZE; i++)
		if(is_occupied(fd_table,i))
			get_file(fd_table, i) = file_duplicate(get_file(fd_table,i));
	
	/* notice success to parent */
	notice_to_parent(current->process,PROCESS_CREATED);
	/* switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	/* notice failure to parent  */
	notice_to_parent(current->process,PROCESS_FAILED);

	/* set error code and terminate */
	current->exit_code = -1;

	thread_exit ();
}
static void notice_to_parent(struct process * process, int status){
	lock_acquire(&process->lock);
	process->status = 1;
	lock_release(&process->lock);
	sema_up(&process->sema);
}

// TODO: Assignment 1. Setup the argument for user program in process_exec() -> load() 편집해야 함!
/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	struct thread* t = thread_current();
	/* We first kill the current context */
	process_cleanup ();

	/* And then load the binary */
	success = load (file_name, &_if);

	/* If load failed, quit. */
	palloc_free_page (file_name);
	if (!success)
		return -1;

	/* Start switched process. */
	/*
		- pintos 코드는 당연하지만 kernel mode
		- intr_frame _if는 process의 권한을 설정함(kernel read/write, user read/write 여부 등)
		- thread에서도 쓰였던 do_iret은 새로운 register를 밀어버려서 바로 새로운 실행흐름을 실행시킴
		  (process_exec == process 실행 == switch kernel mode to user mode)
		- user process로 실행흐름이 옮겨간 뒤 모든 user process실행 뒤에는 exit() system call로 process는 종료됨
		- 따라서 아래 부분 NOT_READCHED는 절대 실행되서는 안되는 거임
	*/ 
	do_iret (&_if); 
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * process of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 * waiting하지 않고 즉시 -1을 return 해야 하는 경우
 * - tid가 정상종료 하지 않고 커널에 의해 죽은 경우
 * - tid is invalid
 * - 내가 부른 자식의 tid가 아닌 경우
 * - 주어진 TID에 대해 process_wait()가 이미 성공적으로 호출된 경우
 * 위 경우 제외하곤 죽은 tid의 exit status return
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. 
 * 
 * */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	int exit_code;
	struct process* process;
	struct list* child_list;
	struct list_elem * elem;
	
	child_list = &thread_current()->child_list;
	
	/* 잘못된 tid 접근 예외 */ 
	if((elem = list_find(child_list,find_process_by_tid,&child_tid)) == NULL)
		return -1;
		
	process = list_entry(elem, struct process, elem);	
	lock_acquire(&process->lock);
	/* 만약 thread가 진행중이라면 */
	if(process->child == PROCESS_TERMINATED){
		lock_release(&process->lock);
		sema_try_down(&process->sema);
	}
	/* Thread가 terminated 된 경우 */
	else{
		lock_release(&process->lock);
		sema_down(&process->sema);
	}
	exit_code = process->exit_code; 
	list_remove(&process->elem);
	free((void*)process);

	return exit_code;
}
static bool
find_process_by_tid(struct list_elem* e_, void* aux)
{
	struct process* c = list_entry(e_, struct process, elem);
	tid_t tid = *(tid_t*)aux;
	return c->tid == tid;
}



/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
  	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	struct fd_table *fd_table;
	struct thread* t;

	t = thread_current();
	/* free fd_table */
	fd_table = get_user_fd(t);
	if(fd_table != NULL){
		for(int i = 2; i < FD_MAX_SIZE; i++)
			if(is_occupied(fd_table,i))
				file_close(get_file(fd_table,i));
		palloc_free_page(fd_table);
	}
	if(t->is_process)
		printf ("%s: exit(%d)\n", t->name, t->exit_code); // process name & exit code

	process_cleanup ();
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

#define USERPROG

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	
	off_t file_ofs;
	bool success = false;
	int i;

	
#ifdef USERPROG /* Argument passing, project 2 */
	char* argv;
	argv = strchr(file_name,' '); 
	if(argv != NULL)
		*argv = '\0';
#endif/* Argument passing, project 2 */
	


	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());
	/* TODO 1: file_name을 쪼개줘야 함 
		- string.c의 strtok_r()함수 사용
		- 반드시 Calling convention(ABI)를 지켜야 한다
			- rsi -> addr of argv[0] 
			- rdi -> argc
		- rip -> user process entry point addr
	*/
	// travers string and set argc

#ifdef USERPROG /* Argument passing, project 2 */
	char *space_ptr = strchr(file_name, ' ');
	if (space_ptr != NULL)
		*space_ptr = '\0';
#endif /* Argument passing, project 2 */

	struct file *file = NULL;
	struct fd_table *table;
	int fd;

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}
	/* fd_table에 추가. */
	file_deny_write(file);
	table = get_user_fd(thread_current());
	
	if( ( fd = find_empty_fd(table)) == FD_ERROR )
	{
		thread_current()->exit_code = -1;
		thread_exit();
	}
	set_fd( table ,fd , file);

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}


	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Set up arguments */
#ifdef USERPROG

	if(argv != NULL)
		*argv = ' ';
	setup_argument(if_, file_name);

#endif

	/* Start address. */
	if_->rip = ehdr.e_entry;  
	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	if (!success && file) {
		file_allow_write(file);
		file_close(file);
	}
	return success;
}

/**********************************************************************/
/* argument passing, project 2 */
#ifdef USERPROG 

static void 
setup_argument(struct intr_frame* if_, const char* file_name)
{	
	char *token, *save_ptr, *argv[64];
	uint32_t argv_size;
	uint64_t argc = 0; 
	size_t tmp_len;
	
	remove_extra_spaces(file_name);  
	
	argv_size = strlen(file_name) + 1;
	if_->rsp -= argv_size;
	memcpy(if_->rsp, file_name, argv_size);

	for(token = strtok_r(if_->rsp, " " , &save_ptr); token != NULL; 
		token = strtok_r(NULL," ", &save_ptr))
	{	
		argv[argc] = token;
		argc++;
	}

	if( argv_size % ALIGNMENT != 0)
	{	
		int size = ALIGNMENT - argv_size % ALIGNMENT ;
		if_->rsp -= size;
		memset(if_->rsp, 0, size);
	}

	//마지막 argv flag 설정
	if_->rsp -= ALIGNMENT; 
	memset(if_->rsp, 0, ALIGNMENT);

	// argv memcpy
	if_->rsp -= argc * ALIGNMENT;
	memcpy(if_->rsp, argv, (argc) * ALIGNMENT);
	
	if_->R.rsi = if_->rsp;
	if_->R.rdi = argc;

	// return address 설정
	if_->rsp -= ALIGNMENT;
	memset(if_->rsp, 0, ALIGNMENT);

}

int find_empty_fd(struct fd_table * fd)
{   
    ASSERT(fd != NULL)
    if(fd == NULL)
        return -1;
    for(int i = 2; i < FD_MAX_SIZE; i++ )
    {
        if( fd->fd_array[i] == 0)
            return i;
    }
    return -1;
}

static char *f_name_to_t_name(const char *file_name, char *t_name) {
	size_t size = strlen(file_name) + 1; // include null terminator
	char *space_ptr = strchr(file_name, ' ');
	if (space_ptr != NULL){
		size = space_ptr - file_name + 1;
	}
		strlcpy(t_name, file_name, size);
	return t_name;
}

#endif
/* argument passing, project 2 */
/**********************************************************************/

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}


#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct load_info *info = (struct load_info*)aux; 

	// file 오프셋 설정
	file_seek(info->file, info->offset);

	// 피일에서 페이지 데이터 로드
	if(file_read(info->file, page->frame->kva, info->page_read_bytes) != (int) info->page_read_bytes) {
		palloc_free_page(page->frame->kva); // 실패시 페이지 메모리 해제
		return false;
	}
	memset(page->frame->kva + info->page_read_bytes, 0, info->page_zero_bytes);
	
	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct load_info *aux = malloc(sizeof(struct load_info));
		struct load_info load_info = {
				.file = file,
				.offset = ofs,
				.page_read_bytes = page_read_bytes,
				.page_zero_bytes = page_zero_bytes,
				.writable = writable
		};

		memcpy(aux, &load_info, sizeof(struct load_info));
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE); // 스택 최상단 - 페이지 사이즈

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	// vm_alloc_page으로 anon페이지 할당하고 쓰기가능한 페이지로 설정
	// vm_claim_page은 해당 페이지를 실제로 물리메로리에 매핑
	success = vm_alloc_page(VM_ANON, stack_bottom, true) &&
								vm_claim_page(stack_bottom);

	if (success)
		if_->rsp = USER_STACK;

	return success;
}
#endif /* VM */
