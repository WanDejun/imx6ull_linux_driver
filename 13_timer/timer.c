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

#define TIMER_CNT 1
#define TIMER_NAME "timer"

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
    atomic_t counter;
    u32 gap_time;
    u8 short_long_gap_flag;
} timer_dev_t;
timer_dev_t timer_dev;

static const struct file_operations timer_dev_fops;

static void timer_func(unsigned long arg) {
    static int sta = 0;

    sta = !sta;
    gpio_set_value(timer_dev.led_gpio, sta);

    if (timer_dev.short_long_gap_flag == 3) {
        timer_dev.timer.expires += msecs_to_jiffies(timer_dev.gap_time >> 1);
        timer_dev.short_long_gap_flag = 0;
    }
    else {
        timer_dev.timer.expires += msecs_to_jiffies((timer_dev.gap_time >> 3));
        timer_dev.short_long_gap_flag++;
    }
    add_timer(&timer_dev.timer); 
    //mod_timer(&timer_dev.timer, jiffies + msecs_to_jiffies(500));
}

ssize_t timer_read(struct file *filp, char __user *buf, size_t cnt, loff_t *lof) {
	int i, ret;
    u32 val, ret_cnt;
    char kernal_buf[16];
	
	for (ret_cnt = 0, val = timer_dev.gap_time; val; val /= 10, ret_cnt++);
    kernal_buf[ret_cnt] = '\0';

    for (i = ret_cnt - 1, val = timer_dev.gap_time; i >= 0; val /= 10, i--) {
        kernal_buf[i] = (val % 10) + '0';
    }

	ret = copy_to_user(buf, kernal_buf, ret_cnt); // 返回定时信息
    if (ret < 0) {
        return ret;
    }

	return ret_cnt;
}

ssize_t timer_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *lof) {
    u8 kernal_buf[16];
    u32 val = 0;
	int ret, i;

	ret = copy_from_user(kernal_buf, buf, cnt); // 读取输入
	if (ret < 0) { // 异常处理
		printk("kernal write failed!\n");
		return ret;
	}

	for (i = 0; i < cnt; i++) {
        if (kernal_buf[i] < '0' || kernal_buf[i] > '9') {
            printk("illegal input!\n");
            return -EINVAL;
        }
        val = val * 10 + kernal_buf[i] - '0';
    }
    
    timer_dev.gap_time = val;

    return 0;
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
    timer_dev.gap_time = 2000;
    timer_dev.short_long_gap_flag = 3;
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
    err = gpio_request(timer_dev.led_gpio, "led1-gpios");
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
     * 7 输出高电平, 关灯
     */
    gpio_set_value(timer_dev.led_gpio, 1);


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
	.owner		= THIS_MODULE,
	.read		= timer_read,
	.write		= timer_write,
	.open		= timer_open,
	.release	= timer_release,
};

/*
 * 模块入口和出口
 */
module_init(timer_init);
module_exit(timer_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WanDejun");