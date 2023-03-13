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

static void user_memory_bound_check(void *address) {
	if (!is_user_vaddr(address) || address == NULL) {
		thread_exit();
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
	// TODO: test current thread name;
	// struct thread *curr = thread_current();
	return process_fork(thread_name, if_);
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
			fork(f->R.rdi, f);
			break;
		case SYS_EXEC:
			break;
		case SYS_WAIT:
			return wait(f->R.rdi);
			break;
		case SYS_CREATE:
			break;
		case SYS_REMOVE:
			break;
		case SYS_OPEN:
			break;
		case SYS_FILESIZE:
			break;
		case SYS_READ:
			break;
		case SYS_WRITE:
			/* Save return value to rax register, which is return value of funciton call */
			/* first argument rdi, second rsi, third rdx */
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			break;
		case SYS_TELL:
			break;
		case SYS_CLOSE:
			break;
	}
	// thread_exit();
}
