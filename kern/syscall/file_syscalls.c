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
    struct vnode *vn;       //Puntatore al file
    off_t offset;	        //Posizione di lettura o scrittura
    unsigned int countRef;
};

struct openfile systemFileTable[SYSTEM_OPEN_MAX];

/**
 * Funzione per incrementare ref count
*/
void openfileIncrRefCount(struct openfile *of) {
  if (of!=NULL)
    of->countRef++;
}

#if USE_KERNEL_BUFFER

static int file_read(int fd, userptr_t buf_ptr, size_t size) {
    struct iovec iov;
    struct uio ku;
    int result, nread;
    struct vnode *vn;
    struct openfile *of;
    void *kbuf;

    if (fd<0 || fd > OPEN_MAX) 
        return -1;
    
    of = curproc->fileTable[fd];
    if (of==NULL) 
        return -1;
    
    vn = of->vn;
    if (vn == NULL) 
        return -1;

    kbuf = kmalloc(size);   //Allocazione buffer di kernel                         
    uio_kinit(&iov, &ku, kbuf, size, of->offset, UIO_READ); //predispozione struttura che dice come fare I/O nel kernel

    result = VOP_READ(vn, &ku);
    if (result) {
        return result;
    }

    of->offset = ku.uio_offset;
    nread = size - ku.uio_resid;

    /**
     * Copia dal buffer di kernel al buffer user
     * Vengono gestite in modo consistenti eventuali errori che si possono avere puntando
     * memoria user non valida
     * 
    */
    copyout(kbuf,buf_ptr,nread); 
    kfree(kbuf);
    return (nread);
}

static int file_write(int fd, userptr_t buf_ptr, size_t size) {
    struct iovec iov;
    struct uio ku;
    int result, nwrite;
    struct vnode *vn;
    struct openfile *of;
    void *kbuf;

    if (fd<0 || fd>OPEN_MAX) 
        return -1;
    
    of = curproc->fileTable[fd];
    if (of == NULL) 
        return -1;
    
    vn = of->vn;
    if (vn == NULL) 
        return -1;

    //copia nel buffer di kernel il contenuto del buffer user
    kbuf = kmalloc(size);
    copyin(buf_ptr,kbuf,size);

    //Scrittura + preparazione struttura
    uio_kinit(&iov, &ku, kbuf, size, of->offset, UIO_WRITE);
    result = VOP_WRITE(vn, &ku);
    if (result) {
        return result;
    }
    kfree(kbuf);
    of->offset = ku.uio_offset;
    nwrite = size - ku.uio_resid;
    return (nwrite);
}

#else  


/**
 * FILE READ e FILE WRITE -> supporto per lettura in memoria user
*/
static int file_read(int fd, userptr_t buf_ptr, size_t size) {
  struct iovec iov;
  struct uio u;
  int result;
  struct vnode *vn;
  struct openfile *of;

  if (fd<0||fd>OPEN_MAX) 
    return -1;
  
  of = curproc->fileTable[fd];
  if (of==NULL) 
    return -1;
  
  vn = of->vn;
  if (vn==NULL) 
    return -1;


  iov.iov_ubase = buf_ptr;          //indirizzo dove andare a scrivere i dati letti da file
  iov.iov_len = size;               //Quanto dobbiamo scrivere

  u.uio_iov = &iov;                 //puntatore a iovec
  u.uio_iovcnt = 1;
  u.uio_resid = size;               // amount to read from the file
  u.uio_offset = of->offset;
  u.uio_segflg = UIO_USERISPACE;    //Lettura in User Space
  u.uio_rw = UIO_READ;              //Lettura
  u.uio_space = curproc->p_addrspace;

  result = VOP_READ(vn, &u);
  if (result) {
    return result;
  }

  of->offset = u.uio_offset;
  return (size - u.uio_resid);
}

static int file_write(int fd, userptr_t buf_ptr, size_t size) {
  struct iovec iov;
  struct uio u;
  int result, nwrite;
  struct vnode *vn;
  struct openfile *of;

  if (fd<0||fd>OPEN_MAX) 
    return -1;
  
  of = curproc->fileTable[fd];
  if (of==NULL) 
    return -1;
  
  vn = of->vn;
  if (vn==NULL) 
    return -1;

  /**
   * Definizione della struttura IOVEC, definisce da dove andare a prendere i dati da scrivere sul file
  */
  iov.iov_ubase = buf_ptr;
  iov.iov_len = size;

  /**
   * Definizione della struttura UIO
  */
  u.uio_iov = &iov;
  u.uio_iovcnt = 1;
  u.uio_resid = size;          // amount to read from the file
  u.uio_offset = of->offset;
  u.uio_segflg = UIO_USERISPACE;
  u.uio_rw = UIO_WRITE;
  u.uio_space = curproc->p_addrspace;

  /* Effettiva scrittura nel file */
  result = VOP_WRITE(vn, &u);
  if (result) {
    return result;
  }
  of->offset = u.uio_offset;
  nwrite = size - u.uio_resid;
  return (nwrite);
}

#endif

int sys_write(int fd, userptr_t buf_ptr, size_t size){
    int i;
    char *p = (char*)buf_ptr;

    /**
     * Caso di un FD di un file
    */
    if(fd != STDOUT_FILENO && fd != STDERR_FILENO){ 
        return file_write(fd, buf_ptr, size);
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
        return file_read(fd, buf_ptr, size);
    }

    for (i = 0; i < (int)size; i++){
        p[i] = getch(); //lettura nel buffer p
        if(p[i] < 0) 
            return i;
    }

    return (int)size;
}


/*
 * file system calls for open/close files
 */
int sys_open(userptr_t path, int openflags, mode_t mode, int *errp){
    int fd, i;
    struct vnode *v;   
    struct openfile *of = NULL; 	
    int result;

    // APERTURA FILE
    result = vfs_open((char *)path, openflags, mode, &v);
    if (result) {
        *errp = ENOENT;
        return -1;
    }

    /* search system-wide open file table */
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
        /* Inserimento nella nella per process file table del fie descriptor */
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
    struct openfile *of = NULL; 
    struct vnode *vn;

    // Controllo sul FD in ingresso
    if (fd<0 || fd>OPEN_MAX) 
        return -1;
    
    // Si salva in locale alla funzione l'open file
    of = curproc->fileTable[fd];
    
    if (of==NULL) 
        return -1;
    
    // Si toglie dalla tabella del file associata al processo
    curproc->fileTable[fd] = NULL;

    if (--of->countRef > 0) 
        return 0; // just decrement ref cnt
    
    // Si elimina il vnode per evitare di lasciare dangling pointer
    vn = of->vn;
    of->vn = NULL;
    
    if (vn == NULL) 
        return -1;

    // Effettiva chiusura del file
    vfs_close(vn); 	
    return 0;
}


