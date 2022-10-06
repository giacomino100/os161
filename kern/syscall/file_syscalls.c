#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>

#include <copyinout.h>
#include <vnode.h>
#include <vfs.h>
#include <limits.h>
#include <uio.h>
#include <proc.h>

/* max num of system wide open files */
#define SYSTEM_OPEN_MAX (10*OPEN_MAX)

/* system open file table */
struct openfile {
    struct vnode *vn;
    off_t offset;	
    unsigned int countRef;
};

struct openfile systemFileTable[SYSTEM_OPEN_MAX];

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


/*
 * file system calls for open/close
 */
int sys_open(userptr_t path, int openflags, mode_t mode, int *errp){
    int fd, i;
    struct vnode *v;   
    struct openfile *of = NULL; 	
    int result;

    result = vfs_open((char *)path, openflags, mode, &v);
    if (result) {
        *errp = ENOENT;
        return -1;
    }

    /* search system open file table */
    for (i=0; i<SYSTEM_OPEN_MAX; i++) {
        if (systemFileTable[i].vn==NULL) {
            of = &systemFileTable[i];
            of->vn = v;
            of->offset = 0; // TODO: handle offset with append
            of->countRef = 1;
            break;
        }
    }

    if (of == NULL) { 
        // no free slot in system open file table
        *errp = ENFILE;
    } else {
        for (fd=STDERR_FILENO+1; fd<OPEN_MAX; fd++) {
            if (curproc->fileTable[fd] == NULL) {
                curproc->fileTable[fd] = of;
                return fd;
            }
        }
        // no free slot in process open file table
        *errp = EMFILE;
    }

    vfs_close(v);
    return -1;
}

/*
 * file system calls for open/close
 */
int sys_close(int fd){
    struct openfile *of=NULL; 
    struct vnode *vn;

    if (fd<0 || fd>OPEN_MAX) 
        return -1;
    
    of = curproc->fileTable[fd];
    
    if (of==NULL) 
        return -1;
    
    curproc->fileTable[fd] = NULL;

    if (of->countRef > 0) 
        return 0; // just decrement ref cnt
    
    vn = of->vn;
    of->vn = NULL;
    
    if (vn==NULL) 
        return -1;

    vfs_close(vn);	
    return 0;
}


