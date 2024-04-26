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
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/atomic.h>
#include <linux/ioctl.h>

#define TIMER_CNT 1
#define TIMER_NAME "ioctl"

#define CLOSE_CMD       _IO(0xEF, 1)
#define OPEN_CMD        _IO(0xEF, 2)
#define SETGAP_CMD      _IOW(0xEF, 3, int)

static char kernel_buf[16];

typedef struct {
    dev_t devid;
    u32 major;
    u32 minor;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
    u32 led_gpio;
    struct timer_list timer;
    atomic_t counter; // 锁
    u8 status; // 点灯状态
    u32 gap_time; // 间隔时间
} timer_dev_t;
timer_dev_t timer_dev;

static const struct file_operations timer_dev_fops;

static void timer_func(unsigned long arg) {
    static int sta = 1;

    sta = !sta;
    gpio_set_value(timer_dev.led_gpio, sta);

    timer_dev.timer.expires += msecs_to_jiffies(timer_dev.gap_time);
    add_timer(&timer_dev.timer); 
    //mod_timer(&timer_dev.timer, jiffies + msecs_to_jiffies(500));
}

ssize_t timer_read(struct file *filp, char __user *buf, size_t cnt, loff_t *lof) {
	int i, ret;
    u32 val, ret_cnt;
	
	for (ret_cnt = 0, val = timer_dev.gap_time; val; val /= 10, ret_cnt++);
    kernel_buf[ret_cnt] = '\0';

    for (i = ret_cnt - 1, val = timer_dev.gap_time; i >= 0; val /= 10, i--) {
        kernel_buf[i] = (val % 10) + '0';
    }

	ret = copy_to_user(buf, kernel_buf, ret_cnt); // 返回定时信息
    if (ret < 0) {
        return ret;
    }

	return ret_cnt;
}

/**
 * @function: set led flash gap time
 */
ssize_t timer_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *lof) {
    u32 val = 0;
	int ret, i;

	ret = copy_from_user(kernel_buf, buf, cnt); // 读取输入
	if (ret < 0) { // 异常处理
		printk("kernel write failed!\n");
		return ret;
	}

	for (i = 0; i < cnt; i++) {
        if (kernel_buf[i] < '0' || kernel_buf[i] > '9') {
            printk("illegal input!\n");
            return -EINVAL;
        }
        val = val * 10 + kernel_buf[i] - '0';
    }
    
    timer_dev.gap_time = val;

    return 0;
}

long timer_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long args) {
    int ret = 0;
    timer_dev_t *dev = filp->private_data;

    switch (cmd) {
	case OPEN_CMD:
        if (dev->status) return 0;

        dev->status = 1;

        dev->timer.expires = jiffies + msecs_to_jiffies(dev->gap_time);
        add_timer(&dev->timer);

        break;
	case CLOSE_CMD:
        del_timer(&dev->timer);
        dev->status = 0;

        break;
    case SETGAP_CMD:

        break;
	default:
        break;
	}

	return ret;
}

long timer_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long args) {
    int ret = 0;
    timer_dev_t *dev = filp->private_data;
    unsigned long *kernel_args = args;

    switch (cmd) {
	case OPEN_CMD:
        if (dev->status) return 0;

        dev->status = 1;

        dev->timer.expires = jiffies + msecs_to_jiffies(dev->gap_time);
        add_timer(&dev->timer);

        break;
	case CLOSE_CMD:
        del_timer(&dev->timer);
        gpio_set_value(timer_dev.led_gpio, 1);

        dev->status = 0;

        break;
    case SETGAP_CMD:
        dev->gap_time = (*kernel_args);

        break;
	default:
        break;
	}

	return ret;
}

int timer_open(struct inode *inode, struct file *filp) {
    filp->private_data = &timer_dev;

    if (atomic_dec_and_test(&timer_dev.counter) == 0) { // 减一后非零，未得到锁
        atomic_inc(&timer_dev.counter);
        return -EBUSY;
    }

    return 0;
}

int timer_release(struct inode *inode, struct file *filp) {
    filp->private_data = NULL;

    atomic_inc(&timer_dev.counter);

    return 0;
}


static int __init timer_init(void) {
    int err;
    atomic_set(&timer_dev.counter, 1);
    timer_dev.gap_time = 500;
    /* 
     * 1 获取设备id 
     */
    timer_dev.major = 0;
    if (timer_dev.major != 0) { // 手动定义设备id
        timer_dev.devid = MKDEV(timer_dev.major, 0);
        err = register_chrdev_region(timer_dev.devid, TIMER_CNT, TIMER_NAME);
    }
    else {
        err = alloc_chrdev_region(&timer_dev.devid, 0, TIMER_CNT, TIMER_NAME);
    }

    if (err < 0) {
        printk("failed_regcdev\n");
        goto failed_regcdev;
    }
    timer_dev.major = MAJOR(timer_dev.devid);
    timer_dev.minor = MINOR(timer_dev.devid);


    /* 
     * 2 注册字符设备
     */
    timer_dev.cdev.owner = THIS_MODULE;
    cdev_init(&timer_dev.cdev, &timer_dev_fops); // 初始化cdev
    err = cdev_add(&timer_dev.cdev, timer_dev.devid, TIMER_CNT);
    if (err < 0) {
        printk("failed_cdev\n");
        goto failed_cdev;
    }


    /* 
     * 3 注册类
     */
    timer_dev.class = class_create(THIS_MODULE, TIMER_NAME); //初始化类
    err = IS_ERR(timer_dev.class);
    if (err < 0) {
        printk("failed_class\n");
        goto failed_class;
    }

    timer_dev.device = device_create(timer_dev.class, NULL, timer_dev.devid, NULL, TIMER_NAME); //初始化设备
    err = IS_ERR(timer_dev.device);
    if (err < 0) {
        printk("failed_device\n");
        goto failed_device;
    }


    /** 
     * 4 获取设备树节点 
     */
    timer_dev.nd = of_find_node_by_path("/alientek/gpioled1");
    if (timer_dev.nd == NULL) {
        err = -EINVAL;
        goto fail_findnode;
    }

    timer_dev.led_gpio = of_get_named_gpio(timer_dev.nd, "led1-gpios", 0);
    if (timer_dev.led_gpio < 0) {
        printk("can not find led gpio!\n");
        err = -EINVAL;
        goto failed_get_gpio;
    }
    printk("led_gpio:%d\n", timer_dev.led_gpio);


    /*
     * 5 申请gpio使用权
     */
    err = gpio_request(timer_dev.led_gpio, "ioctl");
    if (err) { // != 0 error
        printk("can not request led gpio!\n");
        goto failed_request_gpio;
    }


    /*
     * 6 使用gpio, 设置为输出
     */
    err = gpio_direction_output(timer_dev.led_gpio, 1);
    if (err) {
        goto failed_gpio_dirction;
    }


    /*
     * 7 输出低电平, 开灯
     */
    gpio_set_value(timer_dev.led_gpio, 0);


    /*
     * 定时器初始化
     */
    init_timer(&timer_dev.timer);

    timer_dev.timer.function = timer_func;
    timer_dev.timer.expires = jiffies + msecs_to_jiffies(timer_dev.gap_time);
    timer_dev.timer.data = (unsigned long)&timer_dev;
    add_timer(&timer_dev.timer);

    return 0;



/**
 *@ funtion: 错误处理
 */
failed_gpio_dirction:
failed_request_gpio:
    gpio_free(timer_dev.led_gpio);
failed_get_gpio:
fail_findnode:
    device_destroy(timer_dev.class, timer_dev.devid);
failed_device:
    class_destroy(timer_dev.class);
failed_class:
    cdev_del(&timer_dev.cdev);
failed_cdev:
    unregister_chrdev_region(timer_dev.devid, TIMER_CNT);
failed_regcdev:
    return err;
}

static void __exit timer_exit(void) {
    /* 关闭定时器 */
    del_timer(&timer_dev.timer);

    /* 设置led关 */
    gpio_set_value(timer_dev.led_gpio, 1);

    /* 释放led-gpio */
    gpio_free(timer_dev.led_gpio);

    /*删除设备*/
    device_destroy(timer_dev.class, timer_dev.devid);

    /*删除类*/
    class_destroy(timer_dev.class);

    /*删除字符设备*/
    cdev_del(&timer_dev.cdev);
    
    /*注销设备号*/
    unregister_chrdev_region(timer_dev.devid, TIMER_CNT);
}

static const struct file_operations timer_dev_fops = {
	.owner		    = THIS_MODULE,
    .compat_ioctl   = timer_compat_ioctl,
	.unlocked_ioctl = timer_unlocked_ioctl,
	.read		    = timer_read,
	.write		    = timer_write,
	.open		    = timer_open,
	.release	    = timer_release,
};

/*
 * 模块入口和出口
 */
module_init(timer_init);
module_exit(timer_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WanDejun");