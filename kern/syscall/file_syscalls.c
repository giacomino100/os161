#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>

int sys_write(int fd, userptr_t buf_ptr, size_t size){
    int i;
    char *p = (char*)buf_ptr;

    if(fd != STDOUT_FILENO && fd != STDERR_FILENO){
        kprintf("La sys_write function supported only to stdout\n");
        return -1;
    }

    for (i = 0; i < (int)size; i++){
        putch(p[i]); //scrittura nel buffer p
    }
    
    return (int)size;
}

int sys_read(int fd, userptr_t buf_ptr, size_t size){
    int i;
    char *p = (char*)buf_ptr;

    if(fd != STDIN_FILENO){
        kprintf("La sys_read function supported only to stdin\n");
        return -1;
    }

    for (i = 0; i < (int)size; i++){
        p[i] = getch(); //lettura nel buffer p
        if(p[i] < 0) return i;
    }

    return (int)size;
}