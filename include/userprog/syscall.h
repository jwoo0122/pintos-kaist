#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

struct lock access_filesys;
void exit(int status);
void close(int fd);
void syscall_init (void);

#endif /* userprog/syscall.h */
