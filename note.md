# run

/bin/sh: 1: ../../tests/make-grade: not found

首先将 src/tests/make-grade 改成 LF

: open: No such file or directory

注意需要先 make clean 然后再 make grade 否则 up to date

修改 src/tests/threads/Grading, src/tests/threads/Rubric.foo

```bash
docker run -it --rm --name pintos --mount type=bind,source=D:/wksp/pintos,target=/home/PKUOS/pintos pkuflyingpig/pintos bash

cd pintos/src/threads/; make

cd build;

# 或者
cd pintos/src/threads/build

pintos --gdb --   # debug

make tests/threads/alarm-priority.result   
rm tests/userprog/sc-boundary-3.output; make tests/userprog/sc-boundary-3.result # test a certain case

make check > ~/pintos/check.txt  # run all tests
make grade > ~/pintos/grade.txt  # run all tests and grade

```

开一个新终端

```bash
docker exec -it pintos bash

cd pintos/src/threads/build; pintos-gdb kernel.o

debugpintos
```

如果这一步网络不通，ctrl+a+x 第一个终端的 qemu

b *0x7c00

load_kernel
b *0x7c7e    

ctrl+leftarrow 卡死



# Lab 1

assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion

地址计算：t->stack = (uint8_t *) t + PGSIZE

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   禁用中断至 schedule() 完成	确保调度决策和上下文切换的原子性，避免中断干扰。
在 schedule() 后恢复中断	恢复的是原线程的中断状态，且仅在原线程重新执行时生效，不影响新线程的中断环境。
这一设计保障了线程调度的可靠性和操作系统的稳定性，是内核编程中同步控制的经典实践。

顺序：

1. 1.1
2. 2.1: basic priority scheduling
3. 3.1: fixed-point real arithmetic
4. 完善 2.1
5. 剩余

You need to decide where to check whether the elapsed time exceeded the sleep time.

## Alarm Clock

调用 timer_sleep() 时，线程会进入 BLOCKED 状态，被加入 `sleep_list` 队列。

每次 timer_interrupt() 被调用时，会递减 `sleep_list` 中所有线程的 `sleep_ticks`，并检查是否有线程已经到期。若有，则将其从 `sleep_list` 中移除，并唤醒（加入就绪队列）。

update：每次一般 kernel thread 有机会 run 时。也就是先更新数据，再调度。

nice：线程从父线程继承 nice。初始线程的 nice 为 0。

priority：thread init 时和每四次 tick 时更新

recent_cpu：第一个线程为 0，其他新线程为父线程的 recent_cpu。每次 timer_intr 时 running thread（非 idle）的 recent_cpu++。每秒用update函数更新所有线程的 recent_cpu （timer_ticks () % TIMER_FREQ == 0）。值可以为负。

load_avg：初始为零，每秒更新。（timer_ticks () % TIMER_FREQ == 0）

TODO：把 timer 换成 semaphore

TODO：格式：col 超字数


struct donation 
{
  struct lock *lock;      
  int priority;     /* Priority of the thread that donated the priority */
  struct list_elem elem;  
};

void donate_priority(struct thread *donatee, int priority, struct lock *lock)
{
  struct donation *d;
  struct list_elem *e;
  bool found = false;

  /* Check if the lock is already in the donations list 
     (A holds lock, B waits for A, C waits for A too)
     update B's priority
     */
  for (e = list_begin (&donatee->donations); e != list_end (&donatee->donations); e = list_next (e))
  {
    d = list_entry (e, struct donation, elem);
    if (d->lock == lock)
    {
      d->priority = priority;
      found = true;
      break;
    }
  }
  if (!found)
  {
    d = (struct donation *) malloc(sizeof(struct donation));
    d->lock = lock;
    d->priority = priority;
    list_push_back(&donatee->donations, &d->elem);
  }

  update_donated_priority(donatee);
  if (donatee->waiting_lock != NULL)
    donate_priority(donatee->waiting_lock->holder, donatee->donated_priority, donatee->waiting_lock);
}

void update_donated_priority(struct thread *t)
{
  int old_priority = t->donated_priority;
  t->donated_priority = t->base_priority;

  struct donation *d;
  struct list_elem *e;
  for (e = list_begin (&t->donations); e != list_end (&t->donations); e = list_next (e))
  {
    d = list_entry (e, struct donation, elem);
    if (d->priority > t->donated_priority)
      t->donated_priority = d->priority;
  }

  if (t->donated_priority != old_priority && t->waiting_lock != NULL)
  {  
    struct thread *holder = t->waiting_lock->holder;
    update_donated_priority(holder);
  }

  if (t->status == THREAD_READY)
  {
    thread_yield();
  }
}

# Lab 2


文件系统没有内部同步，并发的访问会干扰彼此。文件大小自创建时就固定了，不能扩展。单文件的数据连续存储。没有子目录。文件名至多 14 个字符。

类 Unix 的延迟删除文件

防止大量错误处理代码妨碍主逻辑的可读性。错误处理时保证锁、buffer 等资源释放。

保证 `exec` 在其程序结束前不会返回。

Consider parent process P with child process C.  How do you
ensure proper synchronization and avoid race conditions when P
>calls wait(C) before C exits?  After C exits?  How do you ensure
>that all resources are freed in each case?  How about when P
>terminates without waiting, before C exits?  After C exits?  Are
>there any special cases?



```bash
docker run -it --rm --name pintos --mount type=bind,source=D:/wksp/pintos,target=/home/PKUOS/pintos pkuflyingpig/pintos bash
# 或者
docker exec -it pintos bash

cd pintos/src/userprog/; make

cd build;

# debug 在新终端
pintos-gdb kernel.o  
debugpintos
# 如果这一步网络不通，ctrl+a+x 第一个终端的 qemu
# ctrl+leftarrow 会卡死

# 测试
rm tests/userprog/read-bad-ptr.output; make tests/userprog/read-bad-ptr.result

rm tests/userprog/no-vm/multi-oom.output; make tests/userprog/no-vm/multi-oom.result

code src/userprog/build/tests/userprog/syn-read.output

make check > ~/pintos/check.txt  # run all tests
make grade > ~/pintos/grade.txt  # run all tests and grade

# 究极五合一版
clear; make; pintos --filesys-size=2 -p ../../examples/echo -a echo -- -f -q run 'echo PKUOS'
clear; make; pintos --gdb --filesys-size=2 -p ../../examples/echo -a echo -- -f -q run 'echo PKUOS'
```

修改 `proc_info->ref_count` 的地方：

1. `process_execute`：创建新进程时，父进程调用 `init_proc_info` 将 `ref_count` 初始化为 1。如果创建线程失败，调用 `free_proc_info_refcnt` 释放 `proc_info`。
2. 子进程执行 `start_process` 时 立即将 `ref_count` 加 1。
3. 子进程加载失败，调用 `free_proc_info_refcnt`，然后 `sema_up`。回到父进程后又调用 `free_proc_info_refcnt`，此时 `proc_info` 被释放.
4. 父进程调用 `process_wait` 时，调用 `free_proc_info_refcnt`。
5. 子进程调用 `syscall_exit` 时，调用 `free_proc_info_refcnt`。

`start_process` 里修改 `proc_info` 不用加锁，因为这时父进程已经在等待了。

一定要记得每次测试通过之后 commit

`process_exit` 只会被 `thread_exit` 调用，而后者会被：

- `pintos_init`
- `kill`
- `exit`
- `start_process` if load failed
- `syscall_exit`

记得在 DESIGNDOC 写得分

exec-bad-ptr

write-bad-fd

open-twice

FAIL tests/userprog/no-vm/multi-oom

FAIL tests/filesys/base/syn-read

FAIL tests/filesys/base/lg-random
[0mRun started 'lg-random' but it never finished

FAIL tests/userprog/exit
Run started 'exit' but it never finished



Executing 'multi-oom':
(multi-oom) begin
multi-oom: exit(-1)
multi-oom: exit(-1)
multi-oom: exit(-1)
multi-oom: exit(-1)
Page fault at 0xc0000000: rights violation error reading page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x804815f
 cr2=c0000000 error=00000005
 eax=c0000000 ebx=bfffffb8 ecx=00000005 edx=00000002
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
multi-oom: exit(-1)
multi-oom: exit(-1)
Page fault at 0xc0000000: rights violation error reading page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x804815f
 cr2=c0000000 error=00000005
 eax=c0000000 ebx=bfffffb8 ecx=00000005 edx=00000002
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
Page fault at 0: not present error writing page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x804814d
 cr2=00000000 error=00000006
 eax=00000000 ebx=bfffffb8 ecx=00000005 edx=00000000
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
Page fault at 0xc0000000: rights violation error writing page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x8048166
 cr2=c0000000 error=00000007
 eax=c0000000 ebx=bfffffb8 ecx=00000005 edx=00000003
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
Page fault at 0: not present error reading page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x8048158
 cr2=00000000 error=00000004
 eax=00000000 ebx=bfffffb8 ecx=00000005 edx=00000001
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
Page fault at 0xc0000000: rights violation error reading page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x804815f
 cr2=c0000000 error=00000005
 eax=c0000000 ebx=bfffffb8 ecx=00000005 edx=00000002
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
Page fault at 0xc0000000: rights violation error reading page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x804815f
 cr2=c0000000 error=00000005
 eax=c0000000 ebx=bfffffb8 ecx=00000005 edx=00000002
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
Page fault at 0xc0000000: rights violation error reading page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x804815f
 cr2=c0000000 error=00000005
 eax=c0000000 ebx=bfffffb8 ecx=00000005 edx=00000002
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
Page fault at 0: not present error reading page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x8048158
 cr2=00000000 error=00000004
 eax=00000000 ebx=bfffffb8 ecx=00000005 edx=00000001
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
multi-oom: exit(-1)
multi-oom: exit(-1)
Page fault at 0xc0000000: rights violation error writing page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x8048166
 cr2=c0000000 error=00000007
 eax=c0000000 ebx=bfffffb8 ecx=00000005 edx=00000003
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
Page fault at 0: not present error reading page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x8048158
 cr2=00000000 error=00000004
 eax=00000000 ebx=bfffffb8 ecx=00000005 edx=00000001
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
Page fault at 0: not present error reading page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x8048158
 cr2=00000000 error=00000004
 eax=00000000 ebx=bfffffb8 ecx=00000005 edx=00000001
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
Page fault at 0xc0000000: rights violation error writing page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x8048166
 cr2=c0000000 error=00000007
 eax=c0000000 ebx=bfffffb8 ecx=00000005 edx=00000003
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
Page fault at 0xc0000000: rights violation error reading page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x804815f
 cr2=c0000000 error=00000005
 eax=c0000000 ebx=bfffffb8 ecx=00000005 edx=00000002
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
Page fault at 0: not present error reading page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x8048158
 cr2=00000000 error=00000004
 eax=00000000 ebx=bfffffb8 ecx=00000005 edx=00000001
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
Page fault at 0: not present error writing page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x804814d
 cr2=00000000 error=00000006
 eax=00000000 ebx=bfffffb8 ecx=00000005 edx=00000000
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
multi-oom: exit(-1)
Page fault at 0: not present error reading page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x8048158
 cr2=00000000 error=00000004
 eax=00000000 ebx=bfffffb8 ecx=00000005 edx=00000001
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
Page fault at 0: not present error reading page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x8048158
 cr2=00000000 error=00000004
 eax=00000000 ebx=bfffffb8 ecx=00000005 edx=00000001
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(43)
multi-oom: exit(-1)
multi-oom: exit(-1)
multi-oom: exit(-1)
multi-oom: exit(-1)
Page fault at 0xc0000000: rights violation error reading page in user context.
multi-oom: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x804815f
 cr2=c0000000 error=00000005
 eax=c0000000 ebx=bfffffb8 ecx=00000005 edx=00000002
 esi=00000000 edi=00000000 esp=bfffff50 ebp=bfffff68
 cs=001b ds=0023 es=0023 ss=0023
multi-oom: exit(-1)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
multi-oom: exit(21)
(multi-oom) after run 1/10, expected depth 43, actual depth 21.: FAILED
multi-oom: exit(1)
Execution of 'multi-oom' complete.
Timer: 277 ticks
Thread: 87 idle ticks, 38 kernel ticks, 152 user ticks
hda2 (filesys): 3851 reads, 170 writes
hda3 (scratch): 82 reads, 2 writes
Console: 9827 characters output
Keyboard: 0 keys pressed
Exception: 19 page faults
Powering off...

ExecutExecution of 'create-bad-ptr' complete.  随机失败