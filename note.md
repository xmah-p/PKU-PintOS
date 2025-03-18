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
rm tests/threads/alarm-priority.output; make tests/threads/alarm-priority.result # test a certain case

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

pintos -v -k -T 480 --bochs  -- -q -mlfqs run mlfqs-block < /dev/null 2> tests/threads/mlfqs-block.errors > tests/threads/mlfqs-block.output
perl -I../.. ../../tests/threads/mlfqs-block.ck tests/threads/mlfqs-block tests/threads/mlfqs-block.result
FAIL tests/threads/mlfqs-block
Test output failed to match any acceptable form.

Acceptable output:
  (mlfqs-block) begin
  (mlfqs-block) Main thread acquiring lock.
  (mlfqs-block) Main thread creating block thread, sleeping 25 seconds...
  (mlfqs-block) Block thread spinning for 20 seconds...
  (mlfqs-block) Block thread acquiring lock...
  (mlfqs-block) Main thread spinning for 5 seconds...
  (mlfqs-block) Main thread releasing lock.
  (mlfqs-block) ...got it.
  (mlfqs-block) Block thread should have already acquired lock.
  (mlfqs-block) end
Differences in `diff -u' format:
  (mlfqs-block) begin
  (mlfqs-block) Main thread acquiring lock.
  (mlfqs-block) Main thread creating block thread, sleeping 25 seconds...
  (mlfqs-block) Block thread spinning for 20 seconds...
  (mlfqs-block) Block thread acquiring lock...
  (mlfqs-block) Main thread spinning for 5 seconds...
  (mlfqs-block) Main thread releasing lock.
- (mlfqs-block) ...got it.
  (mlfqs-block) Block thread should have already acquired lock.
  (mlfqs-block) end