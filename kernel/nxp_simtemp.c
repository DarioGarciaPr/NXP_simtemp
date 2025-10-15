#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#define DEVICE_NAME "nxp_simtemp"
#define DEFAULT_THRESHOLD 45000
#define DEFAULT_SAMPLING_MS 100

struct sample {
    int temp_mC;  // temperatura en milicelsius
    int alert;
};

static struct sample latest_sample;
static int threshold = DEFAULT_THRESHOLD;
static int sampling_ms = DEFAULT_SAMPLING_MS;
struct nxp_simtemp_stats {
    u64 samples_generated;
    u64 invalid_writes;
    u64 alerts;
};
static struct nxp_simtemp_stats stats;
static DEFINE_MUTEX(stats_lock);

static struct timer_list sample_timer;
static DECLARE_WAIT_QUEUE_HEAD(sample_wq);
static bool sample_ready = false;
static DEFINE_MUTEX(sample_lock);

/* --- sysfs interfaces --- */
static ssize_t threshold_show(struct device *dev,
                              struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", threshold);
}

static ssize_t threshold_store(struct device *dev,
                               struct device_attribute *attr,
                               const char *buf, size_t count)
{
    int val;
    if (kstrtoint(buf, 10, &val) == 0) {
        mutex_lock(&sample_lock);
        threshold = val;
        mutex_unlock(&sample_lock);
        return count;
    }
    
    if (kstrtoint(buf, 10, &val) != 0) {
    mutex_lock(&stats_lock);
    stats.invalid_writes++;
    mutex_unlock(&stats_lock);
    return -EINVAL;
    }
    return -EINVAL;
}

static ssize_t nxp_simtemp_write(struct file *file, const char __user *buf,
                                 size_t count, loff_t *ppos)
{
    char kbuf[16];
    int val;

    if (count >= sizeof(kbuf))
        return -EINVAL;

    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;

    kbuf[count] = '\0';

    if (kstrtoint(kbuf, 10, &val) == 0) {
        mutex_lock(&sample_lock);
        threshold = val;
        mutex_unlock(&sample_lock);
        return count;
    }
    
    if (kstrtoint(kbuf, 10, &val) != 0) {
    mutex_lock(&stats_lock);
    stats.invalid_writes++;
    mutex_unlock(&stats_lock);
    return -EINVAL;
    }

    return -EINVAL;
}

static DEVICE_ATTR_RW(threshold);

static ssize_t sampling_show(struct device *dev,
                             struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", sampling_ms);
}

static ssize_t sampling_store(struct device *dev,
                              struct device_attribute *attr,
                              const char *buf, size_t count)
{
    int val;
    if (kstrtoint(buf, 10, &val) == 0 && val > 0) {
        sampling_ms = val;
        mod_timer(&sample_timer, jiffies + msecs_to_jiffies(sampling_ms));
        return count;
    }
    
    if (kstrtoint(buf, 10, &val) != 0 || val <= 0) {
    mutex_lock(&stats_lock);
    stats.invalid_writes++;
    mutex_unlock(&stats_lock);
    return -EINVAL;
}
    
    return -EINVAL;
}
static DEVICE_ATTR_RW(sampling);

static ssize_t stats_show(struct device *dev,
                          struct device_attribute *attr, char *buf)
{
    ssize_t ret;
    mutex_lock(&stats_lock);
    ret = scnprintf(buf, 256,
                    "samples=%llu invalid_writes=%llu alerts=%llu\n",
                    stats.samples_generated,
                    stats.invalid_writes,
                    stats.alerts);
    mutex_unlock(&stats_lock);
    return ret;
}

static DEVICE_ATTR_RO(stats);

/* --- periodic sample generation --- */
static void timer_callback(struct timer_list *t)
{
    mutex_lock(&sample_lock);
    latest_sample.temp_mC = 20000 + (get_random_u32() % 40000); // 20-60 C
    latest_sample.alert = (latest_sample.temp_mC > threshold) ? 1 : 0;
    sample_ready = true;
    wake_up_interruptible(&sample_wq);
    mutex_unlock(&sample_lock);
    
    mutex_lock(&stats_lock);
    stats.samples_generated++;
    if (latest_sample.alert)
    stats.alerts++;
    mutex_unlock(&stats_lock);


    mod_timer(&sample_timer, jiffies + msecs_to_jiffies(sampling_ms));
}

/* --- device file operations --- */
static ssize_t nxp_simtemp_read(struct file *file, char __user *buf,
                                size_t count, loff_t *ppos)
{
    char kbuf[128];
    ssize_t ret = 0;
    struct sample s;

    if (wait_event_interruptible(sample_wq, sample_ready))
        return -ERESTARTSYS;

    mutex_lock(&sample_lock);
    s = latest_sample;
    sample_ready = false;
    mutex_unlock(&sample_lock);

    ret = scnprintf(kbuf, sizeof(kbuf), "%lu temp=%d.%03dC alert=%d\n",
                    jiffies,
                    s.temp_mC / 1000,
                    s.temp_mC % 1000,
                    s.alert);

    if (copy_to_user(buf, kbuf, min(count, (size_t)ret)))
        return -EFAULT;

    return ret;
}

static unsigned int nxp_simtemp_poll(struct file *file, poll_table *wait)
{
    unsigned int mask = 0;
    poll_wait(file, &sample_wq, wait);
    if (sample_ready)
        mask = POLLIN | POLLRDNORM;
    return mask;
}

static const struct file_operations nxp_simtemp_fops = {
    .owner = THIS_MODULE,
    .read = nxp_simtemp_read,
    .poll = nxp_simtemp_poll,
    .write = nxp_simtemp_write,
};

static struct miscdevice nxp_simtemp_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &nxp_simtemp_fops,
};

/* --- core init/exit (existing functionality) --- */
static int nxp_simtemp_init_core(void)
{
    int ret;

    ret = misc_register(&nxp_simtemp_dev);
    if (ret)
        return ret;

    ret = device_create_file(nxp_simtemp_dev.this_device, &dev_attr_threshold);
    if (ret)
        goto err_remove;

    ret = device_create_file(nxp_simtemp_dev.this_device, &dev_attr_sampling);
    if (ret)
        goto err_remove_threshold;
        
    ret = device_create_file(nxp_simtemp_dev.this_device, &dev_attr_stats);
    if (ret)
        goto err_remove_sampling;

    timer_setup(&sample_timer, timer_callback, 0);
    mod_timer(&sample_timer, jiffies + msecs_to_jiffies(sampling_ms));

    pr_info("nxp_simtemp loaded\n");
    return 0;
    
    err_remove_sampling:
    device_remove_file(nxp_simtemp_dev.this_device, &dev_attr_sampling);
    goto err_remove_threshold;


err_remove_threshold:
    device_remove_file(nxp_simtemp_dev.this_device, &dev_attr_threshold);
err_remove:
    misc_deregister(&nxp_simtemp_dev);
    return ret;
}

static void nxp_simtemp_cleanup(void)
{
    del_timer_sync(&sample_timer);
    device_remove_file(nxp_simtemp_dev.this_device, &dev_attr_threshold);
    device_remove_file(nxp_simtemp_dev.this_device, &dev_attr_sampling);
    device_remove_file(nxp_simtemp_dev.this_device, &dev_attr_stats);
    misc_deregister(&nxp_simtemp_dev);

    pr_info("nxp_simtemp: module unloaded and sysfs cleaned\n");
}

/* --- Platform device for DT testing --- */
static struct platform_device *nxp_simtemp_pdev;

static int nxp_simtemp_platform_init(void)
{
    nxp_simtemp_pdev = platform_device_register_simple("nxp_simtemp", -1, NULL, 0);
    if (IS_ERR(nxp_simtemp_pdev))
        return PTR_ERR(nxp_simtemp_pdev);
    pr_info("nxp_simtemp: platform device created for DT test\n");
    return 0;
}

static void nxp_simtemp_platform_exit(void)
{
    if (nxp_simtemp_pdev)
        platform_device_unregister(nxp_simtemp_pdev);
}

/* --- wrapper init/exit for module --- */
static int __init nxp_simtemp_init_wrapper(void)
{
    int ret;

    ret = nxp_simtemp_init_core();
    if (ret)
        return ret;

    ret = nxp_simtemp_platform_init();
    if (ret) {
        nxp_simtemp_cleanup();
        return ret;
    }

    return 0;
}

static void __exit nxp_simtemp_exit_wrapper(void)
{
    nxp_simtemp_platform_exit();
    nxp_simtemp_cleanup();
}

module_init(nxp_simtemp_init_wrapper);
module_exit(nxp_simtemp_exit_wrapper);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Darío García");
MODULE_DESCRIPTION("Simulated temperature sensor with periodic samples, poll support and DT test device");

