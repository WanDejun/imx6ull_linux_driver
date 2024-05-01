## select
- 1.https://blog.csdn.net/weixin_42462202/article/details/95315926
### 函数原型
```C
extern int select (int __nfds, fd_set *__restrict __readfds,
		   fd_set *__restrict __writefds,
		   fd_set *__restrict __exceptfds,
		   struct timeval *__restrict __timeout);
```
__nfds为待监视的最大文件描述符+1
__readfds, __writefds, __exceptfds为需要监测的读写异常文件对象集合
__timeout为超时时间

### 源码分析
struct timeval *__restrict __timeout:
```C
struct timeval {
	__kernel_time_t		tv_sec;		/* seconds */
	__kernel_suseconds_t	tv_usec;	/* microseconds */
};
```

fd_set类型:
```C
typedef long int __fd_mask;
#define __FD_SETSIZE		1024
#define __NFDBITS	(8 * (int) sizeof (__fd_mask))

/* fd_set for select and pselect.  */
typedef struct
{
    __fd_mask fds_bits[__FD_SETSIZE / __NFDBITS];
} fd_set;
```
从上面可以看出，fd_set其实就是一个数组，内核用一个位来表示一个文件描述符，从内核定义来看，一共有1024个位

下面再来看看这四个设置函数: 见1.csdn

select的系统调用:
```C
SYSCALL_DEFINE5(select, int, n, fd_set __user *, inp, fd_set __user *, outp,
		fd_set __user *, exp, struct timeval __user *, tvp)
{
    /* 从应用层会传递过来三个需要监听的集合，可读，可写，异常 */
    ret = core_sys_select(n, inp, outp, exp, tvp);
    
    return ret;
}
```
SYSCALL_DEFINE5->core_sys_select

core_sys_select将用户空间的数据拷贝到内核空间, 并调用do_select函数, 等待do_select函数返回后,将内核空间的返回值拷贝回用户空间
```C
int core_sys_select(int n, fd_set __user *inp, fd_set __user *outp,
			   fd_set __user *exp, struct timespec *end_time)
{
    /* 在栈上分配一段内存 */
    long stack_fds[SELECT_STACK_ALLOC/sizeof(long)];
    
    size = FDS_BYTES(n); //n个文件描述符需要多少个字节
    
    /* 
     * 如果栈上的内存太小，那么就重新分配内存
     * 为什么是除以6呢？
     * 因为每个文件描述符要占6个bit（输入：可读，可写，异常；输出结果：可读，可写，异常）
     */
    if (size > sizeof(stack_fds) / 6)
		bits = kmalloc(6 * size, GFP_KERNEL);
    
    /* 设置好bitmap指针对应的内存空间 */
    fds.in      = bits; //可读
	fds.out     = bits +   size; //可写
	fds.ex      = bits + 2*size; //异常
	fds.res_in  = bits + 3*size; //返回结果，可读
	fds.res_out = bits + 4*size; //返回结果，可写
	fds.res_ex  = bits + 5*size; //返回结果，异常
    
    /* 将应用层的监听集合拷贝到内核空间 */
    get_fd_set(n, inp, fds.in);
    get_fd_set(n, outp, fds.out);
    get_fd_set(n, exp, fds.ex);
    
    /* 清空三个输出结果的集合 */
	zero_fd_set(n, fds.res_in);
	zero_fd_set(n, fds.res_out);
	zero_fd_set(n, fds.res_ex);
    
    /* 调用do_select阻塞，满足条件时返回 */
    ret = do_select(n, &fds, end_time);
    
    /* 将结果拷贝回应用层 */
    set_fd_set(n, inp, fds.res_in);
    set_fd_set(n, outp, fds.res_out);
    set_fd_set(n, exp, fds.res_ex);
    
    return ret;
}

```
core_sys_select->do_select
```C
// n: 待监测的文件描述符数量
// fds: 内核空间结构体,用于保存select结果
// end_time: 超时时间
int do_select(int n, fd_set_bits *fds, struct timespec *end_time)
{
	ktime_t expire, *to = NULL;
	struct poll_wqueues table;
	poll_table *wait;
	int retval, i, timed_out = 0;
	unsigned long slack = 0;
	unsigned int busy_flag = net_busy_loop_on() ? POLL_BUSY_LOOP : 0;
	unsigned long busy_end = 0;

	rcu_read_lock();
	retval = max_select_fd(n, fds);
	rcu_read_unlock();

	if (retval < 0)
		return retval;
	n = retval;

	poll_initwait(&table);
	wait = &table.pt;
	if (end_time && !end_time->tv_sec && !end_time->tv_nsec) {
		wait->_qproc = NULL;
		timed_out = 1;
	}

	if (end_time && !timed_out)
		slack = select_estimate_accuracy(end_time);

	retval = 0;
	for (;;) {
		unsigned long *rinp, *routp, *rexp, *inp, *outp, *exp;
		bool can_busy_loop = false;
        
        /*可读, 可写, 异常标志位*/
		inp = fds->in; outp = fds->out; exp = fds->ex;
        /*可读, 可写, 异常返回值*/
		rinp = fds->res_in; routp = fds->res_out; rexp = fds->res_ex;

		for (i = 0; i < n; ++rinp, ++routp, ++rexp) {
			unsigned long in, out, ex, all_bits, bit = 1, mask, j;
			unsigned long res_in = 0, res_out = 0, res_ex = 0;

			in = *inp++; out = *outp++; ex = *exp++;
			all_bits = in | out | ex;
			if (all_bits == 0) {
				i += BITS_PER_LONG;
				continue;
			}

			for (j = 0; j < BITS_PER_LONG; ++j, ++i, bit <<= 1) {
				struct fd f;
				if (i >= n)
					break;
				if (!(bit & all_bits))
					continue;
				f = fdget(i);
				if (f.file) {
					const struct file_operations *f_op;
					f_op = f.file->f_op;
					mask = DEFAULT_POLLMASK;
					if (f_op->poll) {
						wait_key_set(wait, in, out,
							     bit, busy_flag);
						mask = (*f_op->poll)(f.file, wait);
					}
					fdput(f);
					if ((mask & POLLIN_SET) && (in & bit)) {
						res_in |= bit;
						retval++;
						wait->_qproc = NULL;
					}
					if ((mask & POLLOUT_SET) && (out & bit)) {
						res_out |= bit;
						retval++;
						wait->_qproc = NULL;
					}
					if ((mask & POLLEX_SET) && (ex & bit)) {
						res_ex |= bit;
						retval++;
						wait->_qproc = NULL;
					}
					/* got something, stop busy polling */
					if (retval) {
						can_busy_loop = false;
						busy_flag = 0;

					/*
					 * only remember a returned
					 * POLL_BUSY_LOOP if we asked for it
					 */
					} else if (busy_flag & mask)
						can_busy_loop = true;

				}
			}
			if (res_in)
				*rinp = res_in;
			if (res_out)
				*routp = res_out;
			if (res_ex)
				*rexp = res_ex;
			cond_resched();
		}
		wait->_qproc = NULL;
		if (retval || timed_out || signal_pending(current))
			break;
		if (table.error) {
			retval = table.error;
			break;
		}

		/* only if found POLL_BUSY_LOOP sockets && not out of time */
		if (can_busy_loop && !need_resched()) {
			if (!busy_end) {
				busy_end = busy_loop_end_time();
				continue;
			}
			if (!busy_loop_timeout(busy_end))
				continue;
		}
		busy_flag = 0;

		/*
		 * If this is the first loop and we have a timeout
		 * given, then we convert to ktime_t and set the to
		 * pointer to the expiry value.
		 */
		if (end_time && !to) {
			expire = timespec_to_ktime(*end_time);
			to = &expire;
		}

		if (!poll_schedule_timeout(&table, TASK_INTERRUPTIBLE,
					   to, slack))
			timed_out = 1;
	}

	poll_freewait(&table);

	return retval;
}
```
do_select会对每个被监测的驱动文件调用poll, 监测一定次数后在poll_schedule_timeout调用时挂起线程

驱动文件的poll方法中会包含poll_wait
```C
static inline void poll_wait(struct file * filp, wait_queue_head_t * wait_address, poll_table *p)
{
	if (p && wait_address)
		p->qproc(filp, wait_address, p);
}
```
p->qproc在之前又被初始化为__pollwait
```C
/* Add a new entry */
static void __pollwait(struct file *filp, wait_queue_head_t *wait_address,
				poll_table *p)
{
	struct poll_wqueues *pwq = container_of(p, struct poll_wqueues, pt);
	struct poll_table_entry *entry = poll_get_entry(pwq);
	if (!entry)
		return;
	entry->filp = get_file(filp);
	entry->wait_address = wait_address;
	entry->key = p->_key;
	init_waitqueue_func_entry(&entry->wait, pollwake);
	entry->wait.private = pwq;
	add_wait_queue(wait_address, &entry->wait);
}
```
add_wait_queue将当前线程添加进对应驱动文件的等待队列中,用于接收驱动信号

当do_select接收到驱动文件的信号是, do_select函数返回信号