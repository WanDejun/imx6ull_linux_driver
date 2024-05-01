## kill_fasync

### 原型
```C
// linux/fs.h
struct fasync_struct {
	spinlock_t		fa_lock;
	int			magic;
	int			fa_fd;
	struct fasync_struct	*fa_next; /* singly linked list(单链表) */
	struct file		*fa_file;
	struct rcu_head		fa_rcu;
};

/* can be called from interrupts */
extern void kill_fasync(struct fasync_struct **, int, int);
```
kill_fasync:
```C
// fs/fcntl.c
void kill_fasync(struct fasync_struct **fp, int sig, int band)
{
	/* First a quick test without locking: usually
	 * the list is empty.
	 */
	if (*fp) {
		rcu_read_lock();
		kill_fasync_rcu(rcu_dereference(*fp), sig, band);
		rcu_read_unlock();
	}
}
EXPORT_SYMBOL(kill_fasync);
```
kill_fasync执行rcu锁相关的一些操作后进入kill_fasync_rcu

kill_fasync->kill_fasync_rcu:
```C
// fs/fcntl.c
static void kill_fasync_rcu(struct fasync_struct *fa, int sig, int band)
{
	while (fa) { // 遍历队列
		struct fown_struct *fown;
		unsigned long flags;

		if (fa->magic != FASYNC_MAGIC) {
			printk(KERN_ERR "kill_fasync: bad magic number in "
			       "fasync_struct!\n");
			return;
		}
		spin_lock_irqsave(&fa->fa_lock, flags);
		if (fa->fa_file) {
			fown = &fa->fa_file->f_owner;
			/* Don't send SIGURG to processes which have not set a
			   queued signum: SIGURG has its own default signalling
			   mechanism. */
			if (!(sig == SIGURG && fown->signum == 0)) // 不发送SIGURG, 并且进程已经接收到一个信号时不发送下一个信号
				send_sigio(fown, fa->fa_fd, band);
		}
		spin_unlock_irqrestore(&fa->fa_lock, flags);
		fa = rcu_dereference(fa->fa_next);
	}
}
```
kill_fasync_rcu->send_sigio
```C
// fs/fcntl.c
void send_sigio(struct fown_struct *fown, int fd, int band)
{
	struct task_struct *p;
	enum pid_type type;
	struct pid *pid;
	int group = 1;
	
	read_lock(&fown->lock);

	type = fown->pid_type;
	if (type == PIDTYPE_MAX) {
		group = 0;
		type = PIDTYPE_PID;
	}

	pid = fown->pid;
	if (!pid)
		goto out_unlock_fown;
	
	read_lock(&tasklist_lock);
	do_each_pid_task(pid, type, p) {
		send_sigio_to_task(p, fown, fd, band, group);
	} while_each_pid_task(pid, type, p);
	read_unlock(&tasklist_lock);
 out_unlock_fown:
	read_unlock(&fown->lock);
}
```
send_sigio通过传入的fown_struct获取到进程id(pid), 调用send_sigio_to_task发送信号到对应进程
send_sigio->send_sigio_to_task
```C
// reason为由kill_fasync传入的信号值
static void send_sigio_to_task(struct task_struct *p,
			       struct fown_struct *fown,
			       int fd, int reason, int group)
{
	/*
	 * F_SETSIG can change ->signum lockless in parallel, make
	 * sure we read it once and use the same value throughout.
	 */
	int signum = ACCESS_ONCE(fown->signum);

	if (!sigio_perm(p, fown, signum))
		return;

	switch (signum) {
		siginfo_t si;
		default:
			/* Queue a rt signal with the appropriate fd as its
			   value.  We use SI_SIGIO as the source, not 
			   SI_KERNEL, since kernel signals always get 
			   delivered even if we can't queue.  Failure to
			   queue in this case _should_ be reported; we fall
			   back to SIGIO in that case. --sct */
			si.si_signo = signum;
			si.si_errno = 0;
		        si.si_code  = reason;
			/* Make sure we are called with one of the POLL_*
			   reasons, otherwise we could leak kernel stack into
			   userspace.  */
			BUG_ON((reason & __SI_MASK) != __SI_POLL);
			if (reason - POLL_IN >= NSIGPOLL)
				si.si_band  = ~0L;
			else
				si.si_band = band_table[reason - POLL_IN];
			si.si_fd    = fd;
			if (!do_send_sig_info(signum, &si, p, group))
				break;
		/* fall-through: fall back on the old plain SIGIO signal */
		case 0:
			do_send_sig_info(SIGIO, SEND_SIG_PRIV, p, group);
	}
}
```
创建并初始化一个siginfo_t si;调用do_send_sig_info发送信号

send_sigio_to_task->do_send_sig_info
```C
// kernal/signal.c
int do_send_sig_info(int sig, struct siginfo *info, struct task_struct *p,
			bool group)
{
	unsigned long flags;
	int ret = -ESRCH;

	if (lock_task_sighand(p, &flags)) {
		ret = send_signal(sig, info, p, group);
		unlock_task_sighand(p, &flags);
	}

	return ret;
}


// kernal/signal.c
static int send_signal(int sig, struct siginfo *info, struct task_struct *t,
			int group)
{
	int from_ancestor_ns = 0;

	return __send_signal(sig, info, t, group, from_ancestor_ns);
}
```
do_send_sig_info->send_signal->__send_signal
```C
// kernal/signal.c
static int __send_signal(int sig, struct siginfo *info, struct task_struct *t,
			int group, int from_ancestor_ns)
{
	struct sigpending *pending;
	struct sigqueue *q;
	int override_rlimit;
	int ret = 0, result;

	assert_spin_locked(&t->sighand->siglock);

	result = TRACE_SIGNAL_IGNORED;
	if (!prepare_signal(sig, t,
			from_ancestor_ns || (info == SEND_SIG_FORCED)))
		goto ret;

	pending = group ? &t->signal->shared_pending : &t->pending;
	/*
	 * Short-circuit ignored signals and support queuing
	 * exactly one non-rt signal, so that we can get more
	 * detailed information about the cause of the signal.
	 */
	result = TRACE_SIGNAL_ALREADY_PENDING;
	if (legacy_queue(pending, sig))
		goto ret;

	result = TRACE_SIGNAL_DELIVERED;
	/*
	 * fast-pathed signals for kernel-internal things like SIGSTOP
	 * or SIGKILL.
	 */
	if (info == SEND_SIG_FORCED)
		goto out_set;

	/*
	 * Real-time signals must be queued if sent by sigqueue, or
	 * some other real-time mechanism.  It is implementation
	 * defined whether kill() does so.  We attempt to do so, on
	 * the principle of least surprise, but since kill is not
	 * allowed to fail with EAGAIN when low on memory we just
	 * make sure at least one signal gets delivered and don't
	 * pass on the info struct.
	 */
	if (sig < SIGRTMIN)
		override_rlimit = (is_si_special(info) || info->si_code >= 0);
	else
		override_rlimit = 0;

	q = __sigqueue_alloc(sig, t, GFP_ATOMIC | __GFP_NOTRACK_FALSE_POSITIVE,
		override_rlimit);
	if (q) {
		list_add_tail(&q->list, &pending->list);
		switch ((unsigned long) info) {
		case (unsigned long) SEND_SIG_NOINFO:
			q->info.si_signo = sig;
			q->info.si_errno = 0;
			q->info.si_code = SI_USER;
			q->info.si_pid = task_tgid_nr_ns(current,
							task_active_pid_ns(t));
			q->info.si_uid = from_kuid_munged(current_user_ns(), current_uid());
			break;
		case (unsigned long) SEND_SIG_PRIV:
			q->info.si_signo = sig;
			q->info.si_errno = 0;
			q->info.si_code = SI_KERNEL;
			q->info.si_pid = 0;
			q->info.si_uid = 0;
			break;
		default:
			copy_siginfo(&q->info, info);
			if (from_ancestor_ns)
				q->info.si_pid = 0;
			break;
		}

		userns_fixup_signal_uid(&q->info, t);

	} else if (!is_si_special(info)) {
		if (sig >= SIGRTMIN && info->si_code != SI_USER) {
			/*
			 * Queue overflow, abort.  We may abort if the
			 * signal was rt and sent by user using something
			 * other than kill().
			 */
			result = TRACE_SIGNAL_OVERFLOW_FAIL;
			ret = -EAGAIN;
			goto ret;
		} else {
			/*
			 * This is a silent loss of information.  We still
			 * send the signal, but the *info bits are lost.
			 */
			result = TRACE_SIGNAL_LOSE_INFO;
		}
	}

out_set:
	signalfd_notify(t, sig);
	sigaddset(&pending->signal, sig);
	complete_signal(sig, t, group);
ret:
	trace_signal_generate(sig, info, t, group, result);
	return ret;
}
```