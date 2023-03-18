#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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

#define STDOUT_FD 1

static struct file *fd_to_file(int fd) {
	struct thread *curr = thread_current();
	struct list_elem *f_fd_elem;
	
	if (!list_empty(&curr->file_descriptors)) {
		for (f_fd_elem = list_begin(&curr->file_descriptors); f_fd_elem != list_end(&curr->file_descriptors); f_fd_elem = list_next(f_fd_elem)) {
			struct file_with_descriptor *f_fd = list_entry(f_fd_elem, struct file_with_descriptor, elem);
			
			if (f_fd->descriptor == fd) {
				return f_fd->_file;
			}
		}
	}
	
	return NULL;
}

static void user_memory_bound_check(void *address) {
	struct thread *curr = thread_current();
	
	if (!is_user_vaddr(address) || address == NULL || pml4_get_page(curr->pml4, address) == NULL) {
		exit(-1);
	}
}

static void halt(void) {
	power_off();
}

static void exit(int status) {
	struct thread *curr = thread_current();
	curr->exit_code = status;
	printf("%s: exit(%d)\n", curr->name, status); 
	/* This will call process_exit */
	thread_exit();
}

static int fork(const char *thread_name, struct intr_frame * if_) {
	tid_t tid = process_fork(thread_name, if_);
	return tid;
}

static int wait(tid_t tid) {
	return process_wait(tid);
}

static int write (int fd, const void *buffer, unsigned size) {
	user_memory_bound_check(buffer);
	
	int writed_buffer_size;
	
	if (fd == STDOUT_FD) {
		/* FIXME: size should not be over than few hundread bytes */
		writed_buffer_size = size;
		putbuf(buffer, size);
	}
	
	return writed_buffer_size;
}

static int exec (const char *cmd_line) {
	user_memory_bound_check(cmd_line);
	
	/* Because in process_exec, page map of current thread is all expired. 
		So you have to copy the data in cmd_line to kernel page, to be used in after process_cleanup.
	*/
	char * copied_cmd_line = palloc_get_page(PAL_ZERO);
	strlcpy(copied_cmd_line, cmd_line, strlen(cmd_line) + 1);
	
	int result = process_exec(copied_cmd_line);
	
	if (result == -1) {
		exit(-1);
	}
}

static bool create(const char *filename, unsigned init_size) {
	user_memory_bound_check(filename);
	return filesys_create(filename, init_size);
}

static int open(const char *filename) {
	user_memory_bound_check(filename);
	struct thread *curr = thread_current();
	
	struct file *_file = filesys_open(filename);
	
	if (_file == NULL) {
		return -1;
	}
	
	int newfd = thread_get_min_fd();

	struct file_with_descriptor *f_fd = malloc(sizeof(struct file_with_descriptor));
	f_fd->_file = _file;
	f_fd->descriptor = newfd;
	
	list_push_back(&curr->file_descriptors, &f_fd->elem);
	
	return newfd;
}

static int filesize(int fd) {
	struct file *f = fd_to_file(fd);
	
	if (f == NULL) {	
		return -1;
	}
	
	return file_length(f);
}

static int read (int fd, void *buffer, unsigned size) {
	user_memory_bound_check(buffer);
	
	struct file *f = fd_to_file(fd);
	return file_read(f, buffer, size);
}

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
}

/* The main system call interface */
/* NOTE: should set rax as return value, cause curernt mode is kernel mode
	So barely return the value does nothing useful.
*/
void
syscall_handler (struct intr_frame *f UNUSED) {
	switch (f->R.rax) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi, f);
			break;
		case SYS_EXEC:
			exec(f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			break;
		case SYS_TELL:
			break;
		case SYS_CLOSE:
			break;
	}
}
