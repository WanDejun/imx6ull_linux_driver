## poll
- poll与select基本相同, 区别在于select采用位图表示监测对象的状态, 而poll使用链表保存待监测的设备文件, 避免了select只能监测1024个对象的问题
  
### 函数原型
```C
__fortify_function __fortified_attr_access (__write_only__, 1, 2) int
poll (struct pollfd *__fds, nfds_t __nfds, int __timeout)
```

### struct pollfd *__fds: 待监测对象的结构体
- fd: 文件描述符指针
- event: 待监测时间
- revent: 返回的可执行事件
```C
/* Data structure describing a polling request.  */
struct pollfd
  {
    int fd;			/* File descriptor to poll.  */
    short int events;		/* Types of events poller cares about.  */
    short int revents;		/* Types of events that actually occurred.  */
  };
```
poll在进行数据从用户空间到内核空间的拷贝之后最终会调用do_poll, 源码如下
```C
static int do_poll(unsigned int nfds,  struct poll_list *list,
		   struct poll_wqueues *wait, struct timespec *end_time)
{
	poll_table* pt = &wait->pt;
	ktime_t expire, *to = NULL;
	int timed_out = 0, count = 0;
	unsigned long slack = 0;
	unsigned int busy_flag = net_busy_loop_on() ? POLL_BUSY_LOOP : 0;
	unsigned long busy_end = 0;

	/* Optimise the no-wait case */
	if (end_time && !end_time->tv_sec && !end_time->tv_nsec) {
		pt->_qproc = NULL;
		timed_out = 1;
	}

	if (end_time && !timed_out)
		slack = select_estimate_accuracy(end_time);

	for (;;) {
		struct poll_list *walk;
		bool can_busy_loop = false;

		for (walk = list; walk != NULL; walk = walk->next) {
			struct pollfd * pfd, * pfd_end;

			pfd = walk->entries;
			pfd_end = pfd + walk->len;
			for (; pfd != pfd_end; pfd++) {
				/*
				 * Fish for events. If we found one, record it
				 * and kill poll_table->_qproc, so we don't
				 * needlessly register any other waiters after
				 * this. They'll get immediately deregistered
				 * when we break out and return.
				 */
				if (do_pollfd(pfd, pt, &can_busy_loop,
					      busy_flag)) {
					count++;
					pt->_qproc = NULL;
					/* found something, stop busy polling */
					busy_flag = 0;
					can_busy_loop = false;
				}
			}
		}
		/*
		 * All waiters have already been registered, so don't provide
		 * a poll_table->_qproc to them on the next loop iteration.
		 */
		pt->_qproc = NULL;
		if (!count) {
			count = wait->error;
			if (signal_pending(current))
				count = -EINTR;
		}
		if (count || timed_out)
			break;

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

		if (!poll_schedule_timeout(wait, TASK_INTERRUPTIBLE, to, slack))
			timed_out = 1;
	}
	return count;
}

```
对每个设备调用了一次do_pollfd, do_pollfd如下
```C
static inline unsigned int do_pollfd(struct pollfd *pollfd, poll_table *pwait,
				     bool *can_busy_poll,
				     unsigned int busy_flag)
{
	unsigned int mask;
	int fd;

	mask = 0;
	fd = pollfd->fd;
	if (fd >= 0) {
		struct fd f = fdget(fd);
		mask = POLLNVAL;
		if (f.file) {
			mask = DEFAULT_POLLMASK;
			if (f.file->f_op->poll) {
				pwait->_key = pollfd->events|POLLERR|POLLHUP;
				pwait->_key |= busy_flag;
				mask = f.file->f_op->poll(f.file, pwait);
				if (mask & busy_flag)
					*can_busy_poll = true;
			}
			/* Mask out unneeded events. */
			mask &= pollfd->events | POLLERR | POLLHUP;
			fdput(f);
		}
	}
	pollfd->revents = mask;

	return mask;
}
```