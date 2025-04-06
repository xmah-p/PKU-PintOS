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

需要关注的文件：

```bash
src/userprog/:

process.h, .c
pagedir.h, .c
syscall.h, .c
exception.h, .c
gdt.h, .c
tss.h, .c

src/filesys/:
filesys.h
file.h
```

文件系统没有内部同步，并发的访问会干扰彼此。文件大小自创建时就固定了，不能扩展。单文件的数据连续存储。没有子目录。文件名至多 14 个字符。

类 Unix 的延迟删除文件

Incidentally, these commands work by passing special commands extract and append on the kernel's command line and copying to and from a special simulated "scratch" partition. If you're very curious, you can look at the pintos script as well as filesys/fsutil.c to learn the implementation details.

```bash
pintos-mkdisk filesys.dsk --filesys-size=2
pintos -- -f -q
pintos -p ../../examples/echo -a echo -- -q
pintos -- -q run 'echo PKUOS'

pintos --filesys-size=2 -p ../../examples/echo -a echo -- -f -q run 'echo PKUOS'
```


You might want to create a clean reference file system disk and copy that over whenever you trash your filesys.dsk beyond a useful state, which may happen occasionally while debugging.

用户栈大小固定，代码段从用户虚拟地址的 0x08048000 开始。

The linker sets the layout of a user program in memory, as directed by a "linker script" that tells it the names and locations of the various program segments. You can learn more about linker scripts by reading the "Scripts" chapter in the linker manual, accessible via info ld.

To view the layout of a particular executable, run objdump (80x86) or i386-elf-objdump (SPARC) with the -p option.

As shown above, your code should start the stack at the very top of the user virtual address space, in the page just below virtual address PHYS_BASE (defined in threads/vaddr.h).

顺序：

1. 传参
2. `halt`
3. `exit`
4. `write`
5. 把 `process_wait` 改成死循环
6. 剩余工作
7. 正确的 `process_wait`

如何安排 `argv[]` 的元素顺序？如何避免栈溢出？

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

cd pintos/src/userprog/; make

cd build;

# 或者
cd pintos/src/userprog/build

pintos --gdb --   # debug

make tests/userprog/alarm-priority.result   
rm tests/userprog/alarm-priority.output; make tests/userprog/alarm-priority.result # test a certain case

make check > ~/pintos/check.txt  # run all tests
make grade > ~/pintos/grade.txt  # run all tests and grade

```

debug：开一个新终端

```bash
docker exec -it pintos bash

cd pintos/src/userprog/build; 
pintos-gdb kernel.o

debugpintos
```

如果这一步网络不通，ctrl+a+x 第一个终端的 qemu

b *0x7c00

load_kernel
b *0x7c7e    

ctrl+leftarrow 卡死

```bash
# 在 pintos/src/userprog/build 目录下执行
pintos-mkdisk filesys.dsk --filesys-size=2    # 创建磁盘
pintos -- -f -q    # 格式化磁盘
pintos -p ../../examples/echo -a echo -- -q   # 将 echo 程序添加到磁盘
pintos -- -q run 'echo PKUOS'    # 运行 echo 程序，输出 PKUOS

# 究极五合一版
clear; make; pintos --filesys-size=2 -p ../../examples/echo -a echo -- -f -q run 'echo PKUOS'

clear; make; pintos --gdb --filesys-size=2 -p ../../examples/echo -a echo -- -f -q run 'echo PKUOS'

rm tests/userprog/create-normal.output; make tests/userprog/create-normal.result # test a certain case

-f -q extract run 'echo PKUOS'

```
