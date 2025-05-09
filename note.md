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
rm tests/userprog/read-boundary.output; make tests/userprog/read-boundary.result
rm tests/filesys/base/syn-write.output; make tests/filesys/base/syn-write.result

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

# Lab 3

下面是对“虚拟内存（VM）项目”背景和说明的完整中文翻译：

---

## 背景  
**源文件**  
你将在 `vm/` 目录下进行本项目的开发。

该目录下只包含 Makefile，唯一与 userprog 项目不同之处是开启了 `-DVM` 选项。

你所有的新代码要么放在新建文件里，要么放在之前项目已经引入的文件里。

以下几个文件可能是你在本项目中第一次接触到的：  
- `devices/block.h`  
- `devices/block.c`  

它们提供了对块设备的扇区级读写接口，你将通过它来访问交换分区（swap partition）。

---

## 内存术语

为了讨论虚拟内存时不产生歧义，需要先介绍以下术语：

### 页面（Page）  
- 又称虚拟页面（virtual page），长度为 4096 字节（页面大小）。  
- 必须按页面对齐（start on a virtual address evenly divisible by the page size）。  
- 32 位虚拟地址可分为 20 位页号（Page Number）和 12 位页内偏移（Offset）：

  ```
                 31               12 11        0
                +-------------------+-----------+
                |    页 号 (VPN)    |  偏 移   |
                +-------------------+-----------+
                        虚拟地址
  ```

- 每个进程都有自己独立的用户页集合，地址都在 `PHYS_BASE`（通常是 `0xc0000000`, 即 3 GB）以下。  
- 内核页则是全局的，与当前运行的线程或进程无关。  
- 内核可以访问用户和内核页面，但用户进程只能访问自己用户空间中的页面。  

 Pintos 提供了若干函数来操作虚拟地址，参见“虚拟地址”部分。

### 物理帧（Frame）  
- 又称物理页面（physical frame），是物理内存中的一段连续、按页对齐的区域，也有 4096 字节。  
- 32 位物理地址同样可分为 20 位帧号和 12 位偏移：

  ```
                 31               12 11        0
                +-------------------+-----------+
                |   帧 号 (PFN)    |  偏 移   |
                +-------------------+-----------+
                        物理地址
  ```

- x86 架构本身不允许直接按物理地址访问内存，Pintos 通过将内核虚拟内存从第一个页面开始逐一映射到物理帧来实现“直通”（identity mapping），从而在内核模式下可用虚拟地址访问物理帧。  
- Pintos 提供了在物理地址和内核虚拟地址之间转换的函数，参见“虚拟地址”部分。

### 页表（Page Table）  
- CPU 用来将虚拟地址（页号）翻译为物理地址（帧号）的数据结构，由 x86 架构规定。  
- Pintos 在 `pagedir.c` 中封装了页表管理代码，参见“页表”一节。  
- 下图展示了翻译过程：

  ```
                            +----------+
           .--------------->|页   表   |---------.
          /                 +----------+          |
     31   |   12 11    0                    31    V   12 11    0
    +-----------+-------+                  +------------+-------+
    | 页 号 (VPN) | 偏 移 |                  | 帧 号 (PFN) | 偏 移 |
    +-----------+-------+                  +------------+-------+
     虚拟地址      |                         物理地址      ^
                    \_____________________________________/
  ```

### 交换槽（Swap Slot）  
- 交换分区（swap partition）上的一段连续、按页对齐的磁盘空间，用于存放被换出的页面。

---

## 资源管理概览

你需要设计以下三种数据结构：

1. **补充页表（Supplemental Page Table）**  
2. **帧表（Frame Table）**  
3. **交换表（Swap Table）**

> **提示**  
> 你可以把相关结构合并在一起。每个结构中的条目要包含哪些字段？每种结构是“全局”（系统级）还是“局部”（每个进程一份）？你也要考虑将它们放在非分页内存中，这样指针就一直有效。

可选的数据结构有：数组、链表、位图（bitmap）、哈希表（hash table）等。  
- 数组直观但可能浪费空间；  
- 链表方便增删但查找慢；  
- 位图（Pintos 已提供）非常适合记录“资源 n 是否已用”；  
- 哈希表（Pintos 已提供）适合大规模动态的增删查。

---

## 管理补充页表

补充页表保存了页表本身表示不了的信息。  
- **页面错误（Page Fault）时**，内核查此表获取该虚拟页的数据来源：文件系统？交换？还是“全零”页面？  
- **进程终止时**，内核查此表释放对应资源。

你可以按“节（segment）”或“页（page）”来组织该表。  
高级方案：把补充信息直接挂到硬件页表条目中（改 `pagedir.c`），但较复杂，仅建议给进阶同学。  

**Page Fault 处理**（修改 `userprog/exception.c` 中的 `page_fault()`）的大致流程：  
1. 在补充页表中查找出错的虚拟页；  
   - 获取数据来源：文件系统？交换？还是“全零”页面？
   - 若实现了共享，则页的数据可能已经在物理页中，只是页表中没有
2. 如果访问无效（例如访问内核地址或写入只读页），终止进程；  
3. 申请（或复用共享时已有的）物理帧
4. 若数据在文件或交换分区，则读入；否则清零；  
5. 更新硬件页表条目，使该虚拟页映射到物理帧。

---

## 管理帧表

帧表记录所有已被用户页面占用的物理帧。  
每条目包含：该帧当前对应的虚拟页指针，以及用于页面置换算法的辅助信息。

- 用户页的物理帧需通过 `palloc_get_page(PAL_USER)` 申请，避免占用内核页池（否则某些测试会挂）。  
- 若无空闲帧，则需 **驱逐（evict）** 某页：  
  1. 用置换算法选出一帧（可用“访问位（accessed）”和“脏位（dirty）”）；  
  2. 从相应页表中移除映射；（除非实现了共享，否则一个物理页只会对应一个虚拟页）
  3. 如需，写回文件或交换；  
  4. 该帧即可重用。

> 如果找不到可驱逐且不写交换区就不能释放的帧（且交换也满了），内核 panic。

### 访问位与脏位  
- CPU 在页面被读/写时自动置“访问位”；在写时置“脏位”。  
- OS 可在驱逐算法中手动清零这些位（CPU 永远不会清零这些位）。  
- 注意别名（alias）问题：同一帧若被多个虚拟页 “映射”，只有实际访问的那个页表条目的位会更新。Pintos 默认还为内核虚拟地址做了别名，你要在驱逐和检测时一并考虑。
- 检查内核虚拟地址和用户虚拟地址的访问位与脏位
- 或者确保内核只使用用户虚拟地址访问用户数据
- 其他别名只会在实现共享的情况下（或 bug）出现

---

## 管理交换表

交换表记录交换槽的“已用/空闲”情况。  
它应支持：  
- 申请一个空闲槽，用来写出（evict）某帧；  
- 释放某槽，当其页面被读回或进程终止；

实现细节：  
- 使用 `BLOCK_SWAP` 设备（调用 `block_get_role()` 获取对应的 `struct block`）。  
- 在 `vm/build` 目录下，用 `pintos-mkdisk swap.dsk --swap-size=n` 创建一个大小为 n MB 的交换盘。  
- 或者在运行时用 `--swap-size=n` 临时指定交换盘大小。  
- 交换槽按需分配（lazy）
  - Reading data pages from the executable and writing them to swap immediately at process startup is not lazy.
  - 不能用交换槽存特定的页
  - 当交换槽的内容被读回物理页时，释放交换槽

## 加载

加载可执行文件时，只建立用户页到文件内容的映射关系，不读入数据。

当第一次访问页时，触发 page fault，在那里加载（lazily load）：  
- idea 1: 将磁盘地址存在 PTE，就像 ICS 里
- idea 2: 其他数据结构（补充页表？）存在 thread？

怎么加载：  
- 把页读入内存
  - 替换策略
- 更新 PTE，用已有的接口 `threads/pte.h`, `userprog/pagedir.h(c)`

在 Lab 3，只需要管理 user pool 里的帧：  
- 确保在之前从未使用过 user pool 的帧
- kernel pool 不会有过大压力

hash 表：用户页到 swap 空间或文件内容的映射，page -> frame 的映射 
- 用 hash 表存 Key，把 key 和 value 放在一个结构体里

process exit 释放资源

磁盘扇区 512 字节，页 4096 字节

帧表：  
- 全局，每个条目对应一个物理帧，存一个用户页（内核页）、用户线程指针。
- 整个帧表被一个全局 lock 保护
- 空闲帧通过 `palloc_get_page(PAL_USER)` 获取，若没有空闲帧，则驱逐一个帧（clock algorithm）
- 驱逐：  
  - 如果脏，写回 swap
  - 清除 PTE
  - 重用其帧
  - 如果 swap 满了，panic

每个线程都拥有一个 pagedir，切换线程时切换 pagedir。内核线程的 pagedir 为 NULL（实际上是 init_page_dir）。


```bash
docker run -it --rm --name pintos --mount type=bind,source=D:/wksp/pintos,target=/home/PKUOS/pintos pkuflyingpig/pintos bash
# 或者
docker exec -it pintos bash

cd pintos/src/vm/; make

cd build;

# debug 在新终端
pintos-gdb kernel.o  
debugpintos
# 如果这一步网络不通，ctrl+a+x 第一个终端的 qemu
# ctrl+leftarrow 会卡死

# 测试
rm build/tests/userprog/open-null.output; make build/tests/userprog/open-null.result  # userprog

rm build/tests/filesys/base/syn-write.output; make build/tests/filesys/base/syn-write.result  # filesys

rm build/tests/vm/page-parallel.output; make build/tests/vm/page-parallel.result            # vm

code src/userprog/build/tests/userprog/syn-read.output

make clean; make grade > ~/pintos/grade.txt  # run all tests and grade
```

类型：清除 void *，为 kpage 设置类型。

如果是从 swap 读出来的，需要置脏位！！！


[x] page-linear
[] page-parallel
[] page-shuffle
[] page-merge-seq
[] page-merge-par
[x] pt-bad-addr
[x] pt-bad-read
[x] pt-write-code
[x] pt-write-code2
[x] pt-grow-bad