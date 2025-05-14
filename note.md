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

# Lab 3a

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

pintos --filesys-size=2 -p build/tests/vm/page-parallel -a pp -- -f -q run 'pp'

# at build/
pintos --gdb -v -k -T 60 --qemu  --filesys-size=2 -p tests/vm/page-parallel -a page-parallel -p tests/vm/child-linear -a child-linear --swap-size=4 -- -q  -f run page-parallel < /dev/null

cd pintos/src/vm/; make grade > ~/pintos/grade.txt  # run all tests and grade

cd build;

# debug 在新终端
pintos-gdb kernel.o  
debugpintos
# 如果这一步网络不通，ctrl+a+x 第一个终端的 qemu
# ctrl+leftarrow 会卡死

# 测试
rm build/tests/userprog/open-null.output; make build/tests/userprog/open-null.result  # userprog

rm build/tests/filesys/base/syn-write.output; make build/tests/filesys/base/syn-write.result  # filesys

rm build/tests/vm/page-linear.output; make build/tests/vm/page-linear.result            # vm
rm build/tests/vm/page-parallel.output; make build/tests/vm/page-parallel.result            # vm

code src/userprog/build/tests/userprog/syn-read.output

make clean; 
make grade > ~/pintos/grade.txt  # run all tests and grade
```

类型：清除 void *，为 kpage 设置类型。

如果是从 swap 读出来的，需要置脏位！！！

spe 也是共享的，需要锁保护！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！

始终使用用户虚拟地址来访问数据，只在 page fault 时通过内核地址访问（这不会造成 accessed 和 dirty 位被设置）。

palloc 也需要锁！

即便只是读 SPE（supplemental page entry）里的数据，也必须先拿上那把 SPE 锁，否则就有可能和别的线程在做驱逐/反驱逐、删除/插入等操作时发生竞态，看见半更新甚至野指针。

为什么读也要锁？

驱逐线程在做 evict 时，会先拿 SPE 锁、移除映射、再释放物理帧。如果你不锁，就有可能在它刚移除映射之后、还没结束整个清理流程之前读到一个“空”或被回收的条目。

反向的“un-evict”（把页调回）也需要 SPE 锁。读操作若不锁，就可能在这中间抢先读到不完整或错误的状态。

唯一不需要拿 SPE 锁的情况，是你能确定该 SPE 自创建以来所有涉及它的字段都只读且从不修改（比如某些常量元信息），并且你已经在更高层（比如全表锁或初始化阶段）保证了它的永远“只读”属性。但在我们的 VM 实现里，SPE 里通常包含诸如状态位、swap 位置、文件偏移等会被驱逐、加载代码修改的字段，所以只读也要锁。

只要一个帧被选定，它就被 pinned（clock

当 P 获得原来属于 Q 的页，首先 pin，然后获取此页相关的 spe 锁，从 Q 页表移除。Q 在重新 swap in 它之前也要获得 spe 锁。

确保锁顺序。先 filesys，再帧锁，再 spt 锁，再 spe 锁。

每个进程有一个 spt 锁，在 page fault 查找或 spt 插入删除时使用。查找时必须先获取 spt 锁，然后获取 spe 锁。这样可以避免死锁的可能性。若一个线程持有页项锁并尝试查找，而另一个线程反之（即持有哈希表锁同时尝试获取页项锁），可能会出现死锁。但我们通过在帧表项中添加一个指向其对应页表项的指针来消除了这种代码路径。

我们为补充页表项设置了锁，只有在获取该锁后，才能驱逐或取消驱逐页面。

我们通过先从 Q 的页表中移除该映射，然后再开始驱逐过程，从而防止 Q 在驱逐中期写入该页（否则会立刻触发页错误）。

具体来说，在我们的页错误处理函数中，我们首先尝试调入页面。如果失败（例如页面无效），我们会尝试扩展栈（stack）。随后，如果处理器当前处于用户上下文中，我们会终止该进程；但如果处于内核上下文，我们会检查特殊变量来向内核传递错误信号。

[x] page-linear
[] page-parallel
[x] page-shuffle
[] page-merge-seq
[] page-merge-par
[x] pt-bad-addr
[x] pt-bad-read
[x] pt-write-code
[x] pt-write-code2
[x] pt-grow-bad

全表锁——只在修改表结构（插入/删除/遍历）时用；

表项锁——对单个 SPE 的状态修改（驱逐、反驱逐、读标志等）时用。


1. 帧复用／驱逐（eviction）逻辑错误
现象
在高并发下，一个子进程在触发页错误，正在把帧写出 swap／文件；此时另一子进程的访问把它又拿走做 evict／加载，造成数据写混乱或访问未映射页。

可能原因

pin/unpin 逻辑有遗漏：在加载或写回过程中，没有把对应 frame 标记为 pinned，导致 clock 算法选中了正在使用的帧。

supplemental page entry（SPE）锁与帧表锁的加解锁顺序不对，导致驱逐和加载同时作用到同一个物理帧。

排查

在 frame_alloc_and_lock()、frame_free()、evict_frame() 等函数里，加日志：什么时候 pin、什么时候 unpin；并打印当前帧被哪个 SPE 持有。

用多进程并发访问同一大数组，制造压力，看是否能重现“正在读文件的帧被立刻踢走”情形。

改进

在所有文件读入（load）和写出（evict）整个过程前后，显式 pin/unpin：

c
复制
编辑
  frame_pin (f);
  do_io_read_or_write (...);
  frame_unpin (f);
驱逐前先拿 SPE 锁，再 pin，再写回，再解锁、unpin。加载同理。

4. 同步／锁遗漏
现象
偶发性的内存访问错误（比如突发页错误，或者直接访问到了非法地址），往往是并发时 hash 表、帧表或 SPE 访问出错。

可能原因

在对 SPE 只读时也忘了拿 SPE 锁，导致读取到不稳定状态。

在调度切换、时钟中断、TLB 刷新等路径上，没有正确保护页目录或 TLB。

排查

全文搜索所有对 supplemental_page_table、frame_table、pagedir 的读写位置，确认每一处都在相应锁保护下。

在高并发场景下，把所有锁的 acquire/release 打日志，找一下有没有“acquire 但没 release”或“use 前没 acquire”。

改进

保证对SPE 任何域的读，都要先 lock_acquire (&spe->lock)，读完再 lock_release。即便只是读 is_swapped、swap_slot、writable 等标志位。

在 process_exit()、page_exit()、page_clear() 等清理路径上，也要先锁后做 hash_delete()、frame_free()。

总结
先验证：每个子进程是否真的拥有独立的页目录+补充页表。

检查加载：BSS 清零、懒加载逻辑。

强化 pin/unpin：任何 I/O 读写都要 pin，避免正在操作的帧被驱逐。

锁的覆盖面：只要访问 SPE 或帧表，记得先拿锁。

按上面思路逐步打日志排查，基本能定位是“段装载”还是“并发驱逐”还是“页表隔离”哪个环节出问题。调通后，再根据性能和并发需求，决定是否再做锁优化。祝调试顺利！


load 的时候获取了文件系统锁，这时候如果在 setup stack 中上下文切换发生 page fault，会重复获取文件系统锁。解决方法：在 setup stack 之前解锁。

检查 upage，尤其是 set pinned

FAIL
Kernel panic in run: PANIC at ../../threads/synch.c:197 in lock_acquire(): assertion `!lock_held_by_current_thread (lock)' failed.
Call stack: 0xc0029abd 0xc0022f43 0xc0030bb4 0xc002dc15 0xc0021df5 0xc0022015 0xc002d405 0xc002d7d2 0xc0030653 0xc003079d 0xc0030c12 0xc002dc15 0xc0021df5 0xc0022015 0x80482b1 0x80480dc 0x8048916
Translation of call stack:
In kernel.o:
0xc0029abd: debug_panic (.../../lib/kernel/debug.c:38)
0xc0022f43: lock_acquire (...../../threads/synch.c:199)
0xc0030bb4: load_page_from_spt (...build/../../vm/page.c:120)
0xc002dc15: page_fault (.../userprog/exception.c:178)
0xc0021df5: intr_handler (..../threads/interrupt.c:367)
0xc0022015: intr_entry (threads/intr-stubs.S:38)
0xc002d405: lookup_page (.../../userprog/pagedir.c:69)
0xc002d7d2: pagedir_is_accessed (...../userprog/pagedir.c:195)
0xc0030653: pick_victim_frame (...build/../../vm/frame.c:74)
0xc003079d: frame_alloc (...uild/../../vm/frame.c:121)
0xc0030c12: load_page_from_spt (...build/../../vm/page.c:140)
0xc002dc15: page_fault (.../userprog/exception.c:178)
0xc0021df5: intr_handler (..../threads/interrupt.c:367)
0xc0022015: intr_entry (threads/intr-stubs.S:38)
In tests/vm/page-parallel:
0x080482b1: shuffle (...ib.c:74 (discriminator 3))
0x080480dc: test_main (...sts/vm/page-parallel.c:20)
0x08048916: random_ulong (...ild/../../lib/random.c:81)
Translations of user virtual addresses above are based on a guess at
the binary to use.  If this guess is incorrect, then those
translations will be misleading.

驱逐查 accessed bit，查 pd 的时候缺页，导致锁重复获取。

frame lock 在外层管理。去掉表项锁

对 pagedir 的访问：唯一的可能冲突发生在，一个线程在驱逐（涉及读脏位、清空页表项），另一个线程在使用该页。（真的有冲突吗？）

需要检查：sptlock, frame lock, filesys lock

测试：不并行，顺序四次 linear 有没有问题？  
- 不并行，顺序两次，似乎没什么问题
- 不并行，顺序三或四次，也出现了污染！
- 进程退出时重置帧系统，OK！
检查 exit！！

page fault加载的时候会在 pagedir 里设置页，在进程退出的时候，

# Lab 3b

**任务 1：栈增长**
**练习 1.1**

**实现栈增长。**

在项目 2 中，用户程序的栈只是一页，位于用户虚拟地址空间的最顶端，因此程序的最大栈空间被限制为 1 页。

现在，如果栈的使用超过当前大小，就要根据需要分配更多页面。

* 只有在访问“看起来像”栈的地址时，才分配额外页面。请设计一种启发式方法，用以区分真正的栈访问和其他访问。

---

#### 注意事项

1. 如果用户程序往栈指针以下的位置写数据，就属于程序错误。因为典型的操作系统可能随时中断进程以传递“信号”，而信号处理会向栈中推送数据。但 80x86 架构的 PUSH 指令会在调整栈指针之前检查访问权限，因此它可能在栈指针以下 4 字节处引发缺页异常。否则，PUSH 指令就不能被简单地重启。同理，PUSHA 一次性向栈里推入 32 字节，所以它可能在栈指针以下 32 字节处缺页。
2. 在系统调用或由用户程序引发的缺页异常处理函数（`syscall_handler()` 或 `page_fault()`）中，可以通过传入的 `struct intr_frame` 结构的 `esp` 成员获取当前用户栈指针值。
3. 如果你在访问用户内存前就验证了指针（参见项目 2 中“访问用户内存”那一节），那么只需处理上述情况。
4. 如果你依赖缺页异常来检测非法内存访问，那么还需处理另一种情况：在内核中发生的缺页。这是因为处理器只有在从用户模式切换到内核模式时才会保存用户栈指针到 `intr_frame`，此时从 `intr_frame` 读取到的 `esp` 值是不确定的，并非用户栈指针。因此，需要在最初从用户模式进入内核时，将 `esp` 保存到 `struct thread` 中，或使用其他方案。
5. 你应该对栈大小设定一个绝对限制，大多数操作系统都会这样做。有些系统将此限制设为可调节（如 Unix 下的 `ulimit` 命令）。在许多 GNU/Linux 系统中，默认限制是 8 MB。
6. 第一页栈页无需延迟分配——可在加载时就分配并用命令行参数初始化。
7. 所有栈页都应可被换出；换出的栈页应写入交换区（swap）。

---

**任务 2：内存映射文件**
**练习 2.1**

文件系统常用 `read` 和 `write` 系统调用进行访问。另一种次级接口是使用 `mmap` 直接将文件映射到虚拟页面，然后程序可用普通内存指令访问文件数据。

* 设文件 foo 长度为 0x1000 字节（4 KB，即一页）。若将 foo 映射到地址 0x5000，那么对虚拟地址 0x5000…0x5FFF 的任何访问，都会对应文件 foo 中的相应字节。

下面示例程序使用 `mmap` 将文件打印到控制台：

```c
#include <stdio.h>
#include <syscall.h>
int main (int argc UNUSED, char *argv[]) 
{
  void *data = (void *) 0x10000000;     /* 指定映射地址 */

  int fd = open (argv[1]);              /* 打开文件 */
  mapid_t map = mmap (fd, data);        /* 映射文件 */
  write (1, data, filesize (fd));       /* 将映射的数据写到控制台 */
  munmap (map);                         /* 取消映射（可选） */
  return 0;
}
```

完整示例（含错误处理）的 `mcat.c` 和 `mcp.c` 位于 examples 目录下。

你的实现必须能够跟踪哪些内存区域被文件映射所占用。需要一个映射表来记录文件与进程虚拟页面的对应关系，以便：

1. 在映射区域发生缺页时正确处理；
2. 确保映射区域不与进程中其他段（如可执行文件加载时的代码段／数据段、栈等）重叠。

---

#### 练习 2.1

**实现内存映射文件，支持以下系统调用：**

* `mapid_t mmap (int fd, void *addr)`
* `void munmap (mapid_t mapping)`

##### 系统调用：`mapid_t mmap (int fd, void *addr)`

1. 将文件描述符 `fd` 对应的已打开文件映射到进程的虚拟地址空间。整个文件应该映射到从 `addr` 开始的连续虚拟页面上。
2. 你的虚拟内存系统必须实现“延迟加载”——即只有第一次访问对应页面时才从文件系统读取，并且将该映射本身作为页面的后台存储。
3. 置换（evict）映射页面时，应将修改过的页面写回映射文件；若文件长度不是整数页，则最后一页超出文件末尾的部分，在缺页加载时将其置为 0，页面写回时舍弃这些部分。
4. 映射成功则返回唯一的“映射 ID”；失败则返回 -1，并且进程的映射关系不应被修改。
5. 以下情况应导致映射失败：

   * 文件长度为零；
   * `addr` 不是页对齐；
   * 映射区域与现有任何映射或其他段（包括栈或可执行加载时映射）重叠；
   * `addr == 0`；
   * `fd` 为 0 或 1（它们分别是标准输入输出，不能映射）。

##### 系统调用：`void munmap (mapid_t mapping)`

1. 取消由 `mapping` 指定的映射，该 ID 必须是本进程之前成功返回且尚未取消映射的映射。
2. 进程退出时（无论是正常退出还是异常终止），所有映射都应自动取消。
3. 取消映射时，所有被写过的页面写回文件，未被写的页面不写；然后将这些页面从进程的页表和映射表中移除。
4. 关闭或删除文件并不自动取消映射；映射在调用 `munmap` 前始终保持有效（符合 Unix 约定）。对同一文件的每个映射都应调用 `file_reopen` 获取独立的文件引用。
5. 若多个进程映射同一文件，不必保证各自看到一致数据；Unix 通过让它们共享同一物理页面来处理，而 `mmap` 系统调用还能让用户选择“共享”或“私有”（写时复制）映射。

为什么在 syscall 中访问 intr_frame->esp 可能是未定义的？

📚 背景知识
1. intr_frame 是什么？
当用户程序发生中断（比如系统调用或缺页）时，Pintos 内核为该中断创建一个 struct intr_frame，它保存了当时的 CPU 寄存器状态（包括 eip, esp, eax 等），以便中断处理完毕后能“恢复”用户程序的执行。

2. 什么时候 esp 是可靠的？
当中断从用户态进入内核态时，CPU 会自动把用户态的 esp（用户栈指针）压入内核栈中，所以 intr_frame->esp 就是当时的用户栈指针 —— 是有意义的！

❗但 syscall 的非法内存引用是个特例
在 Pintos 中：

syscall_handler() 是处理系统调用的函数；

系统调用是通过 int 0x30 实现的，它触发一个软中断；

当系统调用进入内核态时，Pintos 也确实通过 intr_frame 得到了用户的寄存器值（包括 esp）；

但是！ 如果用户程序传入了一个非法地址（比如 sys_write(1, (void*)0x12345678, 10)，指向了无效内存），然后 Pintos 内核在没有做检查的情况下访问了这个地址——

⚠️ 错误访问不会再触发用户态的 page fault，而是：
在内核态触发了 page fault；

这时进入 page_fault()，但注意：

CPU 不会保存原本的用户 esp，因为现在出错发生在内核态！

所以这时候的 intr_frame->esp：

是内核态的 esp，不是用户态的！

所以它的值对“判断栈访问是否合法”是无效/未定义的；

如果你在 page_fault() 中使用了这个 esp 来决定是否是“栈增长”访问，就会判断错误，甚至出错。

✅ 正确的做法
为了可靠获取“进入内核之前的用户栈指针”，你需要在进入内核的最初时刻，也就是在 syscall_handler() 开头，主动把 intr_frame->esp 保存下来，例如：

c
复制
编辑
struct thread *t = thread_current();
t->user_esp = f->esp;  // f 是 syscall_handler 的 intr_frame* 参数
之后即使发生 page fault 时 intr_frame->esp 不可靠，也可以从 thread_current()->user_esp 获取当时的用户栈指针。