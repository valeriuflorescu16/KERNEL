/* Copyright (C) 2017 Daniel Page <csdsp@bristol.ac.uk>
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "hilevel.h"

/* We assume there will be two user processes, stemming from execution of the 
 * two user programs P1 and P2, and can therefore
 * 
 * - allocate a fixed-size process table (of PCBs), and then maintain an index 
 *   into it to keep track of the currently executing process, and
 * - employ a fixed-case of round-robin scheduling: no more processes can be 
 *   created, and neither can be terminated, so assume both are always ready
 *   to execute.
 */

pcb_t procTab[MAX_PROCS];
pcb_t *executing = NULL;
uint32_t stack_offset = 0x1000;
uint32_t activeprocs = 1;
fd_t fdtable[MAX_FDS];

void print(char *x, int n)
{
  for (int i = 0; i < n; i++)
  {
    PL011_putc(UART0, x[i], true);
  }
}

void dispatch(ctx_t *ctx, pcb_t *prev, pcb_t *next)
{
  char prev_pid = '?', next_pid = '?';

  if (NULL != prev)
  {
    memcpy(&prev->ctx, ctx, sizeof(ctx_t)); // preserve execution context of P_{prev}
    prev_pid = '0' + prev->pid;
  }
  if (NULL != next)
  {
    memcpy(ctx, &next->ctx, sizeof(ctx_t)); // restore  execution context of P_{next}
    next_pid = '0' + next->pid;
  }

  PL011_putc(UART0, '[', true);
  PL011_putc(UART0, prev_pid, true);
  PL011_putc(UART0, '-', true);
  PL011_putc(UART0, '>', true);
  PL011_putc(UART0, next_pid, true);
  PL011_putc(UART0, ']', true);

  executing = next; // update   executing process to P_{next}

  return;
}

void schedule(ctx_t *ctx)
{

  int priorityy = 0;
  int current = 0;
  int next = 0;

  int prboundary = -1;
  for (int i = 0; i < activeprocs; i++)
  {
    priorityy = procTab[i].priority + procTab[i].age + procTab[i].niceness;

    if ((priorityy > prboundary) && ((procTab[i].status == STATUS_READY) || (procTab[i].status == STATUS_EXECUTING)))
    {

      next = i;
      prboundary = priorityy;
    }
  }
  // resetting the age of the next executing process
  procTab[next].age = 0;

  // aging the other processes that are not executing
  for (int i = 0; i < activeprocs; i++)
  {
    if (i != next)
    {
      procTab[i].age += 1;
    }
  }

  // finding out which process is the current executing one
  for (int i = 0; i < activeprocs; i++)
  {
    if (executing->pid == procTab[i].pid)
    {
      current = i;
    }
  }

  //doing the dispatch
  dispatch(ctx, &procTab[current], &procTab[next]);
  if (procTab[current].status == STATUS_EXECUTING)
    (procTab[current].status == STATUS_READY); //update execution status of current
  procTab[next].status = STATUS_EXECUTING;     //update execution status of next

  return;
}

extern void main_console();
extern uint32_t tos_console;
extern uint32_t tos_general;

void hilevel_handler_rst(ctx_t *ctx)
{
  
  PL011_putc(UART0, 'A', true);
  TIMER0->Timer1Load = 0x00100000;  // select period = 2^20 ticks ~= 1 sec
  TIMER0->Timer1Ctrl = 0x00000002;  // select 32-bit   timer
  TIMER0->Timer1Ctrl |= 0x00000040; // select periodic timer
  TIMER0->Timer1Ctrl |= 0x00000020; // enable          timer interrupt
  TIMER0->Timer1Ctrl |= 0x00000080; // enable          timer

  GICC0->PMR = 0x000000F0;         // unmask all            interrupts
  GICD0->ISENABLER1 |= 0x00000010; // enable timer          interrupt
  GICC0->CTLR = 0x00000001;        // enable GIC interface
  GICD0->CTLR = 0x00000001;        // enable GIC distributor

  int_enable_irq();

  /* Invalidate all entries in the process table, so it's clear they are not
   * representing valid (i.e., active) processes.
   */

  for (int i = 0; i < MAX_PROCS; i++)
  {
    procTab[i].status = STATUS_INVALID;
  }

  /* Automatically execute the user programs P1 and P2 by setting the fields
   * in two associated PCBs.  Note in each case that
   *    
   * - the CPSR value of 0x50 means the processor is switched into USR mode, 
   *   with IRQ interrupts enabled, and
   * - the PC and SP values match the entry point and top of stack. 
   */

  for (int i = 0; i < 3; i++)
  {
    fdtable[i].free = false;
  }

  for (int i = 3; i < MAX_FDS; i++)
  {
    fdtable[i].free = true;
  }

  memset(&procTab[0], 0, sizeof(pcb_t)); // initialise 0-th PCB
  procTab[0].pid = 0;
  procTab[0].status = STATUS_READY;
  procTab[0].tos = (uint32_t)(&tos_console);
  procTab[0].ctx.cpsr = 0x50;
  procTab[0].ctx.pc = (uint32_t)(&main_console);
  procTab[0].ctx.sp = (uint32_t)(&tos_console);
  procTab[0].priority = 15;
  procTab[0].age = 0;
  procTab[0].niceness = 0;

  dispatch(ctx, NULL, &procTab[0]);

  return;
}

void hilevel_handler_irq(ctx_t *ctx)
{
  // Step 2: read  the interrupt identifier so we know the source.

  uint32_t id = GICC0->IAR;

  // Step 4: handle the interrupt, then clear (or reset) the source.

  if (id == GIC_SOURCE_TIMER0)
  {
    TIMER0->Timer1IntClr = 0x01;
    schedule(ctx);
  }

  // Step 5: write the interrupt identifier to signal we're done.

  GICC0->EOIR = id;

  return;
}

void hilevel_handler_svc(ctx_t *ctx, uint32_t id)
{
  /* Based on the identifier (i.e., the immediate operand) extracted from the
   * svc instruction, 
   *
   * - read  the arguments from preserved usr mode registers,
   * - perform whatever is appropriate for this system call, then
   * - write any return value back to preserved usr mode registers.
   */

  switch (id)
  {
  case 0x00:
  { // 0x00 => yield()
    PL011_putc(UART0, 'Y', true);
    schedule(ctx);

    break;
  }

  case 0x01:
  { // 0x01 => write( fd, x, n )
    //PL011_putc(UART0, 'W', true);
    int fd = (int)(ctx->gpr[0]);
    char *x = (char *)(ctx->gpr[1]);
    int n = (int)(ctx->gpr[2]);

    if (fd == 0)
    {
      ctx->gpr[0] = 0;
    }
    else if (fd == 1)
    {
      for (int i = 0; i < n; i++)
      {
        PL011_putc(UART0, *x++, true);
      }
      ctx->gpr[0] = n;
    }
    else if (fd < 0)
    {
      ctx->gpr[0] = -1; //error
    }
    else if (fd == 2)
    {
      ctx->gpr[0] = -1; //error
    }
    else
    {

      pipe_t *pipe_main = fdtable[fd].file;

      for (int i = 0; i < n; i++)
      {
        if (pipe_main->length == buffersize)
        {
          break;
        }
        else if (pipe_main->length < buffersize)
        {
          pipe_main->buffer[pipe_main->head] = *x;
          pipe_main->head = (pipe_main->head + 1) % buffersize;
          pipe_main->length++;
          x++;
        }
        ctx->gpr[0] = i;
      }
    }
    break;
  }

  case 0x02:
  { // 0x02 => read (fd, x, n)
    //PL011_putc(UART0, 'R', true);
    int fd = (int)(ctx->gpr[0]);
    char *x = (char *)(ctx->gpr[1]);
    int n = (int)(ctx->gpr[2]);

    if (fd == 0)
    {
      ctx->gpr[0] = 0;
    }
    else if (fd == 1)
    {
      ctx->gpr[0] = 0;
    }
    else if (fd < 0)
    {
      ctx->gpr[0] = -1; //error
    }
    else if (fd == 2)
    {
      ctx->gpr[0] = -1; //error
    }
    else
    {
      pipe_t *pipe_main = fdtable[fd].file;

      for (int i = 0; i < n; i++)
      {
        if (pipe_main->length == 0)
        {
          break;
        }
        else if (pipe_main->length > 0)
        {
          *(x + i) = pipe_main->buffer[pipe_main->tail];
          pipe_main->tail = (pipe_main->tail + 1) % buffersize;
          pipe_main->length--;
        }
        ctx->gpr[0] = i;
      }
    }
    break;
  }

  case 0x03:
  { // 0x03 => fork
    PL011_putc(UART0, 'F', true);
    int child = -1;

    for (int i = 1; i < MAX_PROCS; i++)
    {
      if (procTab[i].status == STATUS_TERMINATED)
      {
        child = i;
        break;
      }
    }

    if (child == -1)
    {
      memset(&procTab[activeprocs], 0, sizeof(pcb_t)); // initialise child PCB
      procTab[activeprocs].tos = (uint32_t)(&tos_general) - (stack_offset * (activeprocs - 1));
      child = activeprocs;
    }

    activeprocs++;

    procTab[child].pid = (pid_t)(child);
    procTab[child].status = STATUS_READY;
    procTab[child].priority = 15;
    procTab[child].age = 0;
    procTab[child].niceness = executing->niceness;
    memcpy(&procTab[child].ctx, ctx, sizeof(ctx_t));

    uint32_t size = (uint32_t)executing->tos - (uint32_t)executing->ctx.sp;
    procTab[child].ctx.sp = procTab[child].tos - size;
    memcpy((uint32_t *)(procTab[child].ctx.sp), (uint32_t *)ctx->sp, size);

    ctx->gpr[0] = procTab[child].pid;
    procTab[child].ctx.gpr[0] = 0;

    break;
  }

  case 0x04:
  { //exit
  //can be tested using P5 
    PL011_putc(UART0, 'E', true);
    executing->status = STATUS_TERMINATED;
    ctx->gpr[0];
    schedule(ctx);

    break;
  }

  case 0x05:
  { //exec
    PL011_putc(UART0, 'X', true);
    // read pointer to the entry point
    ctx->pc = (uint32_t)(ctx->gpr[0]);

    // reset stack pointer
    ctx->sp = executing->tos;

    break;
  }

  case 0x06:
  { //kill
    PL011_putc(UART0, 'K', true);
    int i = (int)(ctx->gpr[0]);
    procTab[i].status = STATUS_TERMINATED;

    schedule(ctx);

    break;
  }

  case 0x07:
  { // nice(pid_t pid, int x)
    pid_t pid = ctx->gpr[0];
    int x = ctx->gpr[1];

    //-20 is highest priority, whilst 19 is the lowest;
    // however, as I have done my priority so that a higher value means a higher priority
    // I will be doing this the other way round, with 19 highest, -20 lowest

    if (x > 19)
    {
      x = 19;
    }
    if (x < -20)
    {
      x = -20;
    }
    procTab[pid].niceness = x;
    break;
  }

  case 0x08:
  { //pipe ( int fds[2] )
    PL011_putc(UART0, 'P', true);
    int *pipefds = (int *)ctx->gpr[0];
    int readfd = -1;
    int writefd = -1;

    //initialise pipe 
    pipe_t *ps = malloc(sizeof(pipe_t));
    ps->head = 0;
    ps->tail = 0;
    ps->length = 0;

    for (int i = 3; i < MAX_FDS; i++)
    {
      if (fdtable[i].free == true)
      {
        readfd = i;
        fdtable[i].free = false;
        fdtable[i].file = ps;
        break;
      }
    }

    for (int i = 3; i < MAX_FDS; i++)
    {
      if (fdtable[i].free == true)
      {
        writefd = i;
        fdtable[i].free = false;
        fdtable[i].file = ps;
        break;
      }
    }
    if (readfd == -1 | writefd == -1) // pipe fail
    {
      ctx->gpr[0] = -1;
    }

    else
    {
      *(pipefds) = readfd;
      *(pipefds + 1) = writefd;

      ctx->gpr[0] = 0; //pipe success
    }
    break;
  }

  default:
  { // 0x?? => unknown/unsupported
    break;
  }
  }

  return;
}
