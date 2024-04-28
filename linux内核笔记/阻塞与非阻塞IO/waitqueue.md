## waitqueue

### 功能
等待队列(waitqueue) 是一种常用的阻塞IO机制, 在引用操作IO时, 会判断设备文件是否繁忙, 若繁忙则当前线程设置为挂起状态(可中断的TASK_INTERRUPTIBLE和不可中断的TASK_UNINTERRUPTIBLE), 并加入等待队列中

当驱动可以被调用时, 会通过weak_up(和其他功能相同的函数)唤醒某个等待队列中的线程(置为TASK_RUNNING, 表示可以被挂载到CPU中执行).

### 等待队列的函数接口
初始化一个等待队列(队列头)
```C
init_waitqueue_head(wait_queue_head_t*);
```

挂起函数
- 为当前线程创建一个等待队列项(wait_queue_t), 并添加进等待队列(头)中wait_queue_head_t
- flag为休眠条件,condition==1时线程不会挂起
```C
wait_enent(wait_queue_head_t*, int condition); // 不可中断睡眠
wait_event_interruptible(wait_queue_head_t*, int condition); // 可中断睡眠
```

唤醒函数
- 唤醒整个等待队列中所有符合条件的线程
```C
wake_up(wait_queue_head_t*); // 唤醒等待队列中所有类型的进程
wake_up_interruptible(wait_queue_head_t*); // 唤醒可被打断的睡眠进程
```

### 等待队列挂起原理
#### 通过接口创建等待队列项进行默认挂起
``` C
// state = TASK_UNINTERRUPTIBLE
// exclusive == ret == 0
// cmd = schedule()
#define ___wait_event(wq, condition, state, exclusive, ret, cmd)	\
({									\
	__label__ __out;						\
	wait_queue_t __wait;						\ // 创建一个默认等待队列项
	long __ret = ret;	/* explicit shadow */			\
									\
	INIT_LIST_HEAD(&__wait.task_list);				\
	if (exclusive)							\
		__wait.flags = WQ_FLAG_EXCLUSIVE;			\
	else								\
		__wait.flags = 0;					\
									\
	for (;;) {							\
		long __int = prepare_to_wait_event(&wq, &__wait, state);\
									\
		if (condition)						\
			break;						\
									\
		if (___wait_is_interruptible(state) && __int) {		\
			__ret = __int;					\
			if (exclusive) {				\
				abort_exclusive_wait(&wq, &__wait,	\
						     state, NULL);	\
				goto __out;				\
			}						\
			break;						\
		}							\
									\
		cmd;							\ // 若分配给当前线程的时间片还没结束, 则主动调用schedule挂起当前线程
	}								\
	finish_wait(&wq, &__wait);					\
__out:	__ret;								\
})
```
prepare_to_wait_event()
```C
// include/linux/wait.c
long prepare_to_wait_event(wait_queue_head_t *q, wait_queue_t *wait, int state)
{
	unsigned long flags;

	if (signal_pending_state(state, current))
		return -ERESTARTSYS;

	wait->private = current;
	wait->func = autoremove_wake_function; // 唤醒函数, 用于设置线程状态为TASK_RUNNING

	spin_lock_irqsave(&q->lock, flags);
	if (list_empty(&wait->task_list)) {
		if (wait->flags & WQ_FLAG_EXCLUSIVE) // 将创建的等待队列项加入等待队列中
			__add_wait_queue_tail(q, wait);
		else
			__add_wait_queue(q, wait);
	}
	set_current_state(state); // 设置当前线程状态(此时线程设置挂起状态但是还未挂起), 由于自旋锁关闭了中断, 所以此时锁一定会被释放之后才可能结束进程, 不会出现死锁
	spin_unlock_irqrestore(&q->lock, flags);

	return 0;
}
```

### 等待队列唤醒原理
wake_up -> __wake_up
```C
void __wake_up(wait_queue_head_t *q, unsigned int mode,
			int nr_exclusive, void *key)
{
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags); // 关中断, 上锁
	__wake_up_common(q, mode, nr_exclusive, 0, key);
	spin_unlock_irqrestore(&q->lock, flags);
}
```
__wake_up -> __wake_up_common
```C
static void __wake_up_common(wait_queue_head_t *q, unsigned int mode,
			int nr_exclusive, int wake_flags, void *key)
{
	wait_queue_t *curr, *next;

	list_for_each_entry_safe(curr, next, &q->task_list, task_list) {
		unsigned flags = curr->flags;

		if (curr->func(curr, mode, wake_flags, key) &&
				(flags & WQ_FLAG_EXCLUSIVE) && !--nr_exclusive)
			break;
	}
}
```

#### 手动创建等待队列, 并挂起
```C
    DECLARE_WAITQUEUE(wait, current); // 创建等待队列
    if (atomic_read(condition) == 0) { // 等待条件
        add_wait_queue(wait_queue_head_t*, wait_queue_t*);
        __set_current_state(TASK_INTERRUPTIBLE); // 置挂起状态
        schedule(); // 挂起线程

		remove_wait_queue(&dev->r_wait, &wait); // 退出等待队列, 由wake_up调用DECLARE_WAITQUEUE初始化时定义的default_wake_function,default_wake_function不会将等待队列项从等待队列中移除

        /*唤醒之后从此处开始运行*/
        if (signal_pending(current)) { // 信号中断唤醒
            __set_current_state(TASK_RUNNING);
            return -ERESTARTSYS;
        }
        __set_current_state(TASK_RUNNING);
    }
```
DECLARE_WAITQUEUE:
```C
#define DECLARE_WAITQUEUE(name, tsk)					\
	wait_queue_t name = __WAITQUEUE_INITIALIZER(name, tsk)


#define __WAITQUEUE_INITIALIZER(name, tsk) {				\
	.private	= tsk,						\
	.func		= default_wake_function,			\
	.task_list	= { NULL, NULL } }


int default_wake_function(wait_queue_t *curr, unsigned mode, int wake_flags, void *key) {
	return try_to_wake_up(curr->private, mode, wake_flags);
}


static int
try_to_wake_up(struct task_struct *p, unsigned int state, int wake_flags)
{
	unsigned long flags;
	int cpu, success = 0;

	/*
	 * If we are going to wake up a thread waiting for CONDITION we
	 * need to ensure that CONDITION=1 done by the caller can not be
	 * reordered with p->state check below. This pairs with mb() in
	 * set_current_state() the waiting thread does.
	 */
	smp_mb__before_spinlock();
	raw_spin_lock_irqsave(&p->pi_lock, flags);
	if (!(p->state & state))
		goto out;

	success = 1; /* we're going to change ->state */
	cpu = task_cpu(p);

	if (p->on_rq && ttwu_remote(p, wake_flags))
		goto stat;

#ifdef CONFIG_SMP
	/*
	 * If the owning (remote) cpu is still in the middle of schedule() with
	 * this task as prev, wait until its done referencing the task.
	 */
	while (p->on_cpu)
		cpu_relax();
	/*
	 * Pairs with the smp_wmb() in finish_lock_switch().
	 */
	smp_rmb();

	p->sched_contributes_to_load = !!task_contributes_to_load(p);
	p->state = TASK_WAKING;

	if (p->sched_class->task_waking)
		p->sched_class->task_waking(p);

	cpu = select_task_rq(p, p->wake_cpu, SD_BALANCE_WAKE, wake_flags);
	if (task_cpu(p) != cpu) {
		wake_flags |= WF_MIGRATED;
		set_task_cpu(p, cpu);
	}
#endif /* CONFIG_SMP */

	ttwu_queue(p, cpu);
stat:
	ttwu_stat(p, cpu, wake_flags);
out:
	raw_spin_unlock_irqrestore(&p->pi_lock, flags);

	return success;
}

```