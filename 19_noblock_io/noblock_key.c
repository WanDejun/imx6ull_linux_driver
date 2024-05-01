/**
 * @function: select/poll 实现非阻塞式io && waitqueue 实现阻塞式io
 * @author: Dejun Wan
 * @date: 2024年4月28日18:25:34
*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/ide.h>
#include <linux/poll.h>

#define KEY_DEV_CNT 1
#define KEY_DEV_NAME "key"
#define KEY_NUM 1
#define KEY_STATUS_ARRAY_NUM (1 + (KEY_NUM >> 5))

typedef struct {
    int gpio;                               // io编号
    int irq_nr;                             // 中断号
    u8 val;                                 // 键值
    char name[10];                          // 名字
    irqreturn_t (*irq_handler)(int, void*); // 中断函数
    struct timer_list timer;
} irq_key_desc_t;

typedef struct {
    dev_t devid;
    u32 major;
    u32 minor;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
    
    irq_key_desc_t irqkey[KEY_NUM];
    irqreturn_t (*(irq_handler[KEY_NUM]))(int, void*);
    void (*(timer_function[KEY_NUM]))(unsigned long);
    struct spinlock irq_lock;
    
    u32 key_status[KEY_STATUS_ARRAY_NUM];
    atomic_t key_update_flag; // 读标志
    wait_queue_head_t r_wait; // 读等待队列
} key_dev_t;
key_dev_t board_key_dev;

static const struct file_operations board_key_dev_fops;

static void timer0_function(unsigned long args) {
    key_dev_t *dev = (key_dev_t *)args;
    int key_val = gpio_get_value(dev->irqkey[0].gpio);
    unsigned long flag;

    spin_lock_irqsave(&dev->irq_lock, flag);
    if (key_val == 0) {
        printk("key0 down!\n");
        dev->key_status[0 << 5] |= (1 << 0);
    }
    else {
        printk("key0 release!\n");
        dev->key_status[0 << 5] &= ~(1 << 0);
    }
    atomic_set(&dev->key_update_flag, 1);
    wake_up(&dev->r_wait);
    spin_unlock_irqrestore(&dev->irq_lock, flag);
}

static irqreturn_t key0_irq_handler(int val, void* dev) { // 中断函数
    key_dev_t* _dev = dev;
    mod_timer(&_dev->irqkey[0].timer, jiffies + msecs_to_jiffies(15));

    return IRQ_HANDLED;
}

ssize_t board_key_read(struct file *filp, char __user *buf, size_t cnt, loff_t *lof) {
    u32 err, copy_size = (((KEY_STATUS_ARRAY_NUM << 5) > cnt) ? cnt : (KEY_STATUS_ARRAY_NUM << 5));
    key_dev_t* dev = (key_dev_t*)filp->private_data;
    char kernel_buf[copy_size];
    int i;
    unsigned long flag;
    
    if (filp->f_flags & O_NONBLOCK) { // 非阻塞
        if (atomic_dec_and_test(&dev->key_update_flag) == 0) { // 无法读
            return -EAGAIN;
        }
    }
    else {
        while (atomic_dec_and_test(&dev->key_update_flag) == 0) { // 尝试获取锁
            int ret = 0;
            atomic_inc(&dev->key_update_flag); // 未获取到
            /*等待事件*/
            ret = wait_event_interruptible(dev->r_wait, dev->key_update_flag.counter);
            if (ret < 0) {
                return -ERESTARTSYS;
            }
        }
    }

    spin_lock_irqsave(&dev->irq_lock, flag);
    for (i = 0; i < copy_size; i++) {
        if (i < KEY_NUM)
            kernel_buf[i] = '0' + (dev->key_status[i >> 5] & (1 << (i & (0x1f))));
        else 
            kernel_buf[i] = '0';
    }
    spin_unlock_irqrestore(&dev->irq_lock, flag);

    err = copy_to_user(buf, kernel_buf, copy_size);
    if (err < 0) return err;
	return copy_size;
}

unsigned int board_key_poll(struct file* filp, struct poll_table_struct* wait) {
    key_dev_t* dev = filp->private_data;

    poll_wait(filp, &dev->r_wait, wait);

    /*判断是否可读*/
    if (atomic_read(&dev->key_update_flag)) {
        return POLLIN | POLLRDNORM;
    }

    return 0;
}

int board_key_open(struct inode *inode, struct file *filp) {
    filp->private_data = &board_key_dev;

    return 0;
}

int board_key_release(struct inode *inode, struct file *filp) {
    filp->private_data = NULL;

    return 0;
}

int key_gpio_init(key_dev_t* dev) {
    int err, i;
    /** 
     * @function: 获取设备树节点 
     */
    dev->nd = of_find_node_by_path("/alientek/key0");
    if (dev->nd == NULL) {
        err = -EINVAL;
        goto fail_findnode;
    }

    /**
     * @function: 循环初始化每一个gpio
    */
    for (i = 0; i < KEY_NUM; i++) {
        dev->irqkey[i].gpio = of_get_named_gpio(dev->nd, "key0-gpios", i);
        if (dev->irqkey[i].gpio < 0) {
            printk("can not find key gpio!\n");
            err = -EINVAL;
            goto failed_get_gpio;
        }
        printk("key_gpio:%d\n", dev->irqkey[i].gpio);

        /**
         * @function: 1 清空name数组
        */
        sprintf(dev->irqkey[i].name, "key%d", i);


        /**
         * @function: 2 申请gpio使用权
         */
        err = gpio_request(dev->irqkey[i].gpio, dev->irqkey[i].name);
        if (err) { // != 0 error
            printk("can not request key gpio!\n");
            goto failed_request_gpio;
        }


        /**
         * @function: 3 使用gpio, 设置为输入
         */
        err = gpio_direction_input(dev->irqkey[i].gpio);
        if (err) {
            goto failed_gpio_dirction;
        }


        /**
         * @funtion: 4 获取中断信息
        */
        dev->irqkey[i].irq_nr = gpio_to_irq(dev->irqkey[i].gpio);
        // dev->irqkey[i].irq_nr = irq_of_parse_and_map(&dev->device, i);


        /**
         * @funtion: 5 中断初始化
        */
        dev->irqkey[i].irq_handler = dev->irq_handler[i]; // 定义中断回调函数
        // 注册中断， 参数为中断号，中断回调函数，中断触发类型，中断名，设备结构体(传给中断回调函数的第二个参数)
        err = request_irq(dev->irqkey[i].irq_nr, dev->irqkey[i].irq_handler, IRQ_TYPE_EDGE_BOTH, dev->irqkey[i].name, dev);

        if (err) {
            printk("irq %d request failed!\n", dev->irqkey[i].irq_nr);
            goto failed_irq_request;
        }

        /**
         * @funtion: 6 下半部定时器初始化
        */
        init_timer(&dev->irqkey[i].timer); 
        dev->irqkey[i].timer.function = dev->timer_function[i];
        dev->irqkey[i].timer.data = (unsigned long)dev;
    }

    
    return 0;

failed_irq_request:
failed_gpio_dirction:
    for (i = 0; i < KEY_NUM; i++) {
        gpio_free(dev->irqkey[i].gpio);
    }
failed_request_gpio:
failed_get_gpio:
fail_findnode:
    return err;
}

void key_gpio_exit(key_dev_t* dev) {
    int i;

    for (i = 0; i < KEY_NUM; i++) {
        free_irq(dev->irqkey[i].irq_nr, dev); // 释放每一个gpio和对应的中断
        gpio_free(dev->irqkey[i].gpio);
        /*删除定时器*/
        del_timer_sync(&board_key_dev.irqkey[i].timer);
    }
}

static int __init board_key_init(void) {
    int err;

    init_waitqueue_head(&board_key_dev.r_wait);
    atomic_set(&board_key_dev.key_update_flag, 0);
    spin_lock_init(&board_key_dev.irq_lock); // 初始化自旋锁 

    board_key_dev.irq_handler[0] = key0_irq_handler; // 定义每个按键对应的中断
    board_key_dev.timer_function[0] = timer0_function; // 定义按键中断的下半部(timer回调,用于消抖)
    
    /**
     * @funtion: 1 获取设备id 
     */
    board_key_dev.major = 0;
    if (board_key_dev.major != 0) { // 手动定义设备id
        board_key_dev.devid = MKDEV(board_key_dev.major, 0); //初始化设备id
        err = register_chrdev_region(board_key_dev.devid, KEY_DEV_CNT, KEY_DEV_NAME); //注册设备id
    }
    else {
        err = alloc_chrdev_region(&board_key_dev.devid, 0, KEY_DEV_CNT, KEY_DEV_NAME); // 分配并注册设备id
    }

    if (err < 0) { // 异常处理
        printk("failed_regcdev\n");
        goto failed_regcdev;
    }
    board_key_dev.major = MAJOR(board_key_dev.devid); // 获取主设备id
    board_key_dev.minor = MINOR(board_key_dev.devid); // 获取次设备id


    /** 
     * @funtion: 2 注册字符设备
     */
    board_key_dev.cdev.owner = THIS_MODULE;
    cdev_init(&board_key_dev.cdev, &board_key_dev_fops); // 初始化cdev
    err = cdev_add(&board_key_dev.cdev, board_key_dev.devid, KEY_DEV_CNT); // 向linux添加cdev
    if (err < 0) {
        printk("failed_cdev\n");
        goto failed_cdev;
    }


    /** 
     * @funtion: 3 注册类
     */
    board_key_dev.class = class_create(THIS_MODULE, KEY_DEV_NAME); //初始化类
    err = IS_ERR(board_key_dev.class); // 判断错误
    if (err < 0) {
        printk("failed_class\n");
        goto failed_class;
    }

    board_key_dev.device = device_create(board_key_dev.class, NULL, board_key_dev.devid, NULL, KEY_DEV_NAME); //初始化设备
    err = IS_ERR(board_key_dev.device); // 判断错误
    if (err < 0) {
        printk("failed_device\n");
        goto failed_device;
    }

    /**
     * @funtion: 4 初始化GPIO
    */
    err = key_gpio_init(&board_key_dev); 
    if (err < 0) {
        goto failed_gpio_init;
    }

    return 0;



/**
 *@ funtion: 错误处理
 */
failed_gpio_init:
    device_destroy(board_key_dev.class, board_key_dev.devid);
failed_device:
    class_destroy(board_key_dev.class);
failed_class:
    cdev_del(&board_key_dev.cdev);
failed_cdev:
    unregister_chrdev_region(board_key_dev.devid, KEY_DEV_CNT);
failed_regcdev:
    return err;
}

static void __exit board_key_exit(void) {
    /*释放gpio*/
    key_gpio_exit(&board_key_dev);

    /*删除设备*/
    device_destroy(board_key_dev.class, board_key_dev.devid);

    /*删除类*/
    class_destroy(board_key_dev.class);

    /*删除字符设备*/
    cdev_del(&board_key_dev.cdev);
    
    /*注销设备号*/
    unregister_chrdev_region(board_key_dev.devid, KEY_DEV_CNT);
}

static const struct file_operations board_key_dev_fops = {
	.owner		= THIS_MODULE,
	.read		= board_key_read,
	.open		= board_key_open,
	.release	= board_key_release,
    .poll       = board_key_poll,
};

/*
 * 模块入口和出口
 */
module_init(board_key_init);
module_exit(board_key_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WanDejun");