#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"

#define PIPESIZE 512

struct pipe {
  // TODO: What does the spinlock exactly do and how does it consist of?
  struct spinlock lock;
  char data[PIPESIZE];
  uint nread;     // number of bytes read
  uint nwrite;    // number of bytes written
  int readopen;   // read fd is still open
  int writeopen;  // write fd is still open
};

// allpcating pipe between two files: one for reading, one for writing
// TODO: why both the parameter types are double pointers?
int
pipealloc(struct file **f0, struct file **f1)
{
  // assign pointer for pipe
  struct pipe *pi;

  // 0 means a null pointer, not a real memory address 
  pi = 0;
  *f0 = *f1 = 0;

  // validation check: after fileallock(), the memory pointer for file should not be null
  // filealloc() assigns the file struct instance, not creates a real file 
  if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
    goto bad;

  // TODO: what does the kallock() exactly do?
  // anyway, it is a validation check including the type casting as a pipe* pointer
  if((pi = (struct pipe*)kalloc()) == 0)
    goto bad;

  // changes the value of the pipe instance
  pi->readopen = 1;
  pi->writeopen = 1;
  pi->nwrite = 0;
  pi->nread = 0;

  // initalize lock from pipe, name for debugging.
  initlock(&pi->lock, "pipe");

  // changes values for file struct
  // the assigned file instances has same pipe instacne on the pipe field.
  // f0 becomes the readable file, f1 becomes the writable file
  // just for fun, cypher expression: (:f0) <-[:read]- (:pipe) -[:write]-> (:f1)
  (*f0)->type = FD_PIPE;
  (*f0)->readable = 1;
  (*f0)->writable = 0;
  (*f0)->pipe = pi;
  (*f1)->type = FD_PIPE;
  (*f1)->readable = 0;
  (*f1)->writable = 1;
  (*f1)->pipe = pi;
  return 0;

 bad:
  if(pi)
    // TODO: what does kfree() exactly do?
    kfree((char*)pi);
  if(*f0)
    fileclose(*f0);
  if(*f1)
    fileclose(*f1);
  return -1;
}

// closes pipe with the 
void
pipeclose(struct pipe *pi, int writable)
{
  acquire(&pi->lock);
  // writable & readopen == 0 
  if(writable){
    pi->writeopen = 0;
    // TODO: why does it wake up the channel - the pointer of the nread? 
    wakeup(&pi->nread);
  } else {
    pi->readopen = 0;
    wakeup(&pi->nwrite);
  }
  if(pi->readopen == 0 && pi->writeopen == 0){
    release(&pi->lock);
    kfree((char*)pi);
  } else
    release(&pi->lock);
}


// write: rf -> wf, n means the channel which the proc is sleeping on
// this is not file-level, it is a pipe-level write method
int
pipewrite(struct pipe *pi, uint64 addr, int n)
{
  int i = 0;
  struct proc *pr = myproc();

  acquire(&pi->lock);
  while(i < n){
    // the reading part is closed or process is killed
    if(pi->readopen == 0 || killed(pr)){
      release(&pi->lock);
      return -1;
    }
    // there is no space to write on the pipe buffer
    if(pi->nwrite == pi->nread + PIPESIZE){ //DOC: pipewrite-full
      // wake up the process on the channel waiting for reading events/taking bytes
      wakeup(&pi->nread);
      // sleep the process on the channel on the pointer of nwrite
      sleep(&pi->nwrite, &pi->lock);
    } else {
      char ch;
      // copyin results to data in user memory copied to kernel
      if(copyin(pr->pagetable, &ch, addr + i, 1) == -1)
        break;
      pi->data[pi->nwrite++ % PIPESIZE] = ch;
      i++;
    }
  }
  wakeup(&pi->nread);
  release(&pi->lock);

  return i;
}

int
piperead(struct pipe *pi, uint64 addr, int n)
{
  int i;
  struct proc *pr = myproc();
  char ch;

  acquire(&pi->lock);
  while(pi->nread == pi->nwrite && pi->writeopen){  //DOC: pipe-empty
    if(killed(pr)){
      release(&pi->lock);
      return -1;
    }
    sleep(&pi->nread, &pi->lock); //DOC: piperead-sleep
  }
  for(i = 0; i < n; i++){  //DOC: piperead-copy
    if(pi->nread == pi->nwrite)
      break;
    ch = pi->data[pi->nread++ % PIPESIZE];
    // copyout makes the value on kernel copied to user
    if(copyout(pr->pagetable, addr + i, &ch, 1) == -1)
      break;
  }
  wakeup(&pi->nwrite);  //DOC: piperead-wakeup
  release(&pi->lock);
  return i;
}
