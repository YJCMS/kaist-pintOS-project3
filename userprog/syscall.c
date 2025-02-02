
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/process.h"



void syscall_entry (void);
void syscall_handler (struct intr_frame *);


/************************************************************************/
/* syscall, project 2  */
#ifdef USERPROG 

#include "filesys/filesys.h"
#include "filesys/file.h"
#include "string.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"

typedef void 
syscall_handler_func(struct intr_frame *);

static syscall_handler_func 
*syscall_handlers[25]; // 25는 총 syscall 갯수;

#define get_fd_table(thread) (thread->fd_table)

static struct file* 
get_file_by_fd(int fd);

static bool
is_vaddr_valid(void* vaddr);

static bool
is_write_valid(void* vaddr);

void 
check_valid_buffer(void *buffer, unsigned size, bool writable, struct intr_frame *f);

static void
halt_handler(struct intr_frame *f);
static void
exit_handler(struct intr_frame* f);
static void 
fork_handler(struct intr_frame* f);
static void
exec_handler(struct intr_frame* f);
static void 
wait_handler(struct intr_frame* f);
static void
create_handler(struct intr_frame* f);
static void
remove_hander(struct intr_frame* f);
static void
read_handler(struct intr_frame* f);
static void 
open_handler(struct intr_frame *f);
static void 
filesize_handler(struct intr_frame* f);
static void  
write_handler(struct intr_frame* f);
static void
seek_handler(struct intr_frame* f);
static void
tell_handler(struct intr_frame* f);
static void
close_handler(struct intr_frame* f);

// project3
static void
mmap_handler(struct intr_frame* f);
static void
munmap_handler(struct intr_frame* f);


#endif
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
	
	
#ifdef USERPROG /* syscall, project 2 */

	// /* 자 지금부터 열려있는 모든 파일은 커널이 관리합니다. */
	memset(syscall_handlers, 0 , sizeof(syscall_handlers)); // sycall 함수배열 초기화

	syscall_handlers[SYS_HALT] = halt_handler;
	syscall_handlers[SYS_EXIT] = exit_handler;
	syscall_handlers[SYS_FORK] = fork_handler;
	syscall_handlers[SYS_EXEC] = exec_handler;
	syscall_handlers[SYS_WAIT] = wait_handler;
	syscall_handlers[SYS_CREATE] = create_handler;
	syscall_handlers[SYS_REMOVE] = remove_hander;
	syscall_handlers[SYS_READ] = read_handler;
	syscall_handlers[SYS_OPEN] = open_handler;
	syscall_handlers[SYS_FILESIZE] = filesize_handler;
	syscall_handlers[SYS_WRITE] = write_handler; 
	syscall_handlers[SYS_SEEK] = seek_handler;
	syscall_handlers[SYS_TELL] = tell_handler;
	syscall_handlers[SYS_CLOSE] = close_handler;
	// project3 mmap 
	syscall_handlers[SYS_MMAP] = mmap_handler;
	//syscall_handlers[SYS_MUNMAP] = munmap_handler;


#endif /* syscall, project 2 */
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {

#ifdef USERPROG /* syscall, project 2 */

	thread_current()->rsp = f->rsp; // project 3 - user rsp 저장
	
	syscall_handler_func *handler;
	int sys_num;
	
	sys_num = f->R.rax;
	handler = syscall_handlers[sys_num];
	if(handler == NULL)
		return;
	handler(f);

#endif /* syscall, project 2 */
}


/******************************************************/
/* userprogram, project 2 */
#ifdef USERPROG

static void
halt_handler(struct intr_frame *f){
	// power_off();
}

static void 
exit_handler(struct intr_frame* f){
	struct thread *current;
	int exit_code;

	current = thread_current();
	exit_code = f->R.rdi;
	current->exit_code = exit_code;
	current->tf.R.rax = exit_code;
	thread_exit();
}

static void 
fork_handler(struct intr_frame* f){
 	char *thread_name;
	int pid;

	thread_name = (char *)f->R.rdi;
	pid = process_fork(thread_name, f);
	f->R.rax = pid;
}

static void
exec_handler(struct intr_frame* f)
{ 	
	int fd;
	const char *fn_copy;
	const char* file_name; 
	
	file_name = (char*)f->R.rdi;
	if((fn_copy =  palloc_get_page(PAL_USER)) == NULL){
		// thread.c 함수화
		thread_exit_by_error(-1);
	}

	if(!is_vaddr_valid(file_name) || *file_name == NULL){
		// thread.c 함수화
		thread_exit_by_error(-1);
	}
	strlcpy(fn_copy, file_name, strlen(file_name) + 1);

	/* exec에 실패할 경우에만 return value가 존재함. */
	f->R.rax = process_exec(fn_copy);
}

static void 
wait_handler(struct intr_frame* f)
{	
	int pid;
	int exit_code;

 	pid = (int)f->R.rdi;
	exit_code = process_wait(pid);
	f->R.rax = exit_code;
}

static void
create_handler(struct intr_frame* f)
{
	char* file_name;
	unsigned initial_size;
	
	file_name = (char*)f->R.rdi;
	initial_size = (unsigned)f->R.rsi;

	if(!is_vaddr_valid(file_name) || *file_name == NULL){
		// thread.c 함수화
		thread_exit_by_error(-1);
	}

	f->R.rax = filesys_create(file_name, initial_size);
}

static void
remove_hander(struct intr_frame* f)
{
	char *file_name;

	file_name = f->R.rdi;
	f->R.rax = filesys_remove(file_name);
}

static void
read_handler(struct intr_frame* f)
{
	struct file* file;
	int fd; 
	void *buffer;
	unsigned size;
	
	fd = f->R.rdi;
	buffer = f->R.rsi;
	size = f->R.rdx;

	check_valid_buffer(buffer, size, true, f);

	if(!is_vaddr_valid(buffer) || !is_write_valid(buffer) || fd == STDOUT_FILENO){
		// thread.c 함수화
		thread_exit_by_error(-1);
	}
	struct thread* t = thread_current();
	if((file = get_file_by_fd(fd))== NULL){
		f->R.rax = -1;
		return;
	}
	f->R.rax = file_read(file, buffer, size);
}

static void 
open_handler(struct intr_frame *f)
{
	struct fd_table* fd_table;
	struct file *file;
	char* file_name;
	int fd; 

	/* Open executable file. */
	file_name = f->R.rdi;
	if(!is_vaddr_valid(file_name) || file_name == NULL){
		// thread.c 함수화
		thread_exit_by_error(-1);
	}

	file = filesys_open (file_name);
	if (file == NULL) {
		f->R.rax = -1;
		return;
	}

	fd_table = get_fd_table(thread_current());
	if( (fd = find_empty_fd(fd_table)) == -1 ){
		file_close(file);
		f->R.rax = -1;
		return;
	}
	set_fd(fd_table,fd,file);
	f->R.rax = fd;
}

static void 
filesize_handler(struct intr_frame* f)
{	
	struct file *file;
	int fd;

	fd = f->R.rdi;

	if((file = get_file_by_fd(fd)) == NULL){
		f->R.rax = -1;
		return;
	}
	f->R.rax = file_length(file);
}

static void  
write_handler(struct intr_frame* f)
{	
	struct file *file;
	int fd;
	void* buffer;
	unsigned size;

	fd = f->R.rdi;
	buffer = f->R.rsi;
	size = f->R.rdx;
	
	check_valid_buffer(buffer, size, true, f);
	
	/* 표준 출력에 작성 */
	if(fd == STDOUT_FILENO)	{
		putbuf(buffer,size);
		return;
	}

	if(!is_vaddr_valid(buffer) || !is_write_valid(buffer) || fd == STDIN_FILENO) {
		// thread.c 함수화
		thread_exit_by_error(-1);
	}
	
	if((file = get_file_by_fd(fd)) == NULL){
		f->R.rax = -1;
		return;
	}
	f->R.rax = file_write(file,buffer,size);
}

static void
seek_handler(struct intr_frame* f)
{
	struct file *file;
	int fd; 
	unsigned position;

	fd = f->R.rdi;
	position = f->R.rsi;

	if((file = get_file_by_fd(fd)) == NULL){
		f->R.rax = -1;
		return;
	}
	file_seek(file,position);
}

static void
tell_handler(struct intr_frame* f)
{
	struct file *file;
	int fd;

	fd = f->R.rdi;
	if((file = get_file_by_fd(fd)) == NULL){
		f->R.rax = -1;
		return;
	}
	f->R.rax = file_tell(file);
}

static void
close_handler(struct intr_frame* f)
{
	struct thread* current;
	struct file *file;
	int fd;

	current = thread_current();
	fd = (int)f->R.rdi;

	if((file = get_file_by_fd(fd)) == NULL){
		// thread.c 함수화
		thread_exit_by_error(-1);
	}
	free_fd(get_fd_table(current),fd);
	file_close(file); 
}

static void
mmap_handler(struct intr_frame* f)  
{
	
	void *addr = f->R.rdi;
	size_t length = f->R.rsi;
	int writable = f->R.rdx;
	int fd = f->R.r10;
	off_t offset = f->R.r8;

    // fd가 0, 1이면 return
	if(fd == 0 || fd == 1) {
		f->R.rax = NULL;
		return;
	}

	// offset 확인
	if(offset != pg_round_down(offset)) {
		f->R.rax = NULL;
		return;
	}

	// fd로 file 가져오기
	struct file *file = get_file_by_fd(fd);
 
	// file의 길이가 null or 0byte이면 return
	if(file == NULL || file_length(file) == 0) {
		thread_exit_by_error(-1);
	}

	// addr가 0이거나 NULL이면 return
	if(addr == NULL || addr != pg_round_down(addr)){
		f->R.rax = NULL;
		return;
    }

	//length가 0이면 return
	if (length == 0) {
		f->R.rax = NULL;
		return;
	}

	// 페이지가 존재하는 경우 return
	if (spt_find_page(&thread_current()->spt, addr)) {
		f->R.rax = NULL;
		return;
	};
    

	return do_mmap(addr, length, writable, file, offset);
};

// static void
// munmap_handler(void *addr) 
// {

// 	return do_munmap(addr);
// };

/***********************************************************/
/* static functions */

static struct file* 
get_file_by_fd(int fd)
{
	struct fd_table *fd_table = get_fd_table(thread_current());
	if(fd < FD_MIN_SIZE || fd > FD_MAX_SIZE)
		return NULL;
	if(is_empty(fd_table,fd))
		return NULL;
	//todo 유효한 파일인지 검사, 이미 닫혔는지 아닌지, 가르키는 주소가 파일이 맞는지.
	return get_file(fd_table,fd);
}

static bool
is_vaddr_valid(void* vaddr)
{
	return !(is_kernel_vaddr(vaddr) 
		//|| pml4_get_page(thread_current()->pml4, vaddr) == NULL 
		|| spt_find_page(&thread_current()->spt, vaddr) == NULL // project3 - read boundary 통과
		|| vaddr == NULL);
}

static bool
is_write_valid(void* vaddr)
{
    struct page *page = spt_find_page(&thread_current()->spt, vaddr);
    return page != NULL && page->writable;
}

void 
check_valid_buffer(void *buffer, unsigned size, bool writable, struct intr_frame *f) {
    void *addr = buffer;

    // 버퍼의 시작부터 크기만큼 반복하면서 모든 주소를 확인
    for (unsigned i = 0; i < size; i += PGSIZE) {
        void *page_addr = pg_round_down(addr + i); // 페이지 단위로 내림해서 확인

        // is_vaddr_valid()로 유효성 검사
        if (!is_vaddr_valid(page_addr)) {
            // 페이지 폴트를 처리할 수 없으면 잘못된 접근으로 판단하고 종료
            if (!vm_try_handle_fault(f, page_addr, true, writable, true)) {
                thread_current()->exit_code = -1;
                thread_exit();
            }
        } else if (writable) {
            // 쓰기 권한이 있는지 확인
            struct page *page = spt_find_page(&thread_current()->spt, page_addr);
            if (!page->writable) {
                // 쓰기 권한이 없는 경우
                thread_current()->exit_code = -1;
                thread_exit();
            }
        }
    }
}

/***********************************************************/

/* Reads a byte at user virtual address UADDR.
 * UADDR must be below KERN_BASE.
 * Returns the byte value if successful, -1 if a segfault
 * occurred. */
static int64_t
get_user (const uint8_t *uaddr) {
    int64_t result;
    __asm __volatile (
    "movabsq $done_get, %0\n"
    "movzbq %1, %0\n"
    "done_get:\n"
    : "=&a" (result) : "m" (*uaddr));
    return result;
}

/* Writes BYTE to user address UDST.
 * UDST must be below KERN_BASE.
 * Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte) {
    int64_t error_code;
    __asm __volatile (
    "movabsq $done_put, %0\n"
    "movb %b2, %1\n"
    "done_put:\n"
    : "=&a" (error_code), "=m" (*udst) : "q" (byte));
    return error_code != -1;
}
#endif
/* userprogram, project 2 */
/******************************************************/
