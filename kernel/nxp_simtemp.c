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
// Data Path binary read
struct sample_record {
    u32 timestamp_jiffies;
    int temp_mC;
    u8 alert;       // 1 si supera threshold
    u8 reserved[3]; // padding a 12 bytes
};

// Flag para alertas de threshold crossing
static bool alert_event = false;

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

/* --- Mode variable --- */
static char sim_mode[16] = "normal";  // default mode
static DEFINE_MUTEX(mode_lock);

/* Ramp generator state */
static int ramp_step = 0;

/* --- Temperature generators --- */
static int generate_normal_temp(void)
{
    return 40000 + (get_random_u32() % 10000); // 25°C ±5°C
}

static int generate_noisy_temp(void)
{
    return 40000 + (get_random_u32() % 20000); // 25°C ±20°C
}

static int generate_ramp_temp(void)
{
    ramp_step += 1000;          // +1°C per sample
    if (ramp_step > 100000)     // reset at 100°C
        ramp_step = 25000;
    return ramp_step;
}

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

static ssize_t mode_show(struct device *dev,
                         struct device_attribute *attr, char *buf)
{
    ssize_t ret;
    mutex_lock(&mode_lock);
    ret = scnprintf(buf, 16, "%s\n", sim_mode);
    mutex_unlock(&mode_lock);
    return ret;
}

static ssize_t mode_store(struct device *dev,
                          struct device_attribute *attr,
                          const char *buf, size_t count)
{
    mutex_lock(&mode_lock);
    if (strncmp(buf, "normal", 6) == 0)
        strcpy(sim_mode, "normal");
    else if (strncmp(buf, "noisy", 5) == 0)
        strcpy(sim_mode, "noisy");
    else if (strncmp(buf, "ramp", 4) == 0)
        strcpy(sim_mode, "ramp");
    mutex_unlock(&mode_lock);
    return count;
}

static DEVICE_ATTR_RW(mode);


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
    //mutex_lock(&mode_lock);
    if (strcmp(sim_mode, "normal") == 0)
      latest_sample.temp_mC = generate_normal_temp(); // 20-60°C
    else if (strcmp(sim_mode, "noisy") == 0)
      latest_sample.temp_mC = generate_noisy_temp(); // 20-100°C
    else if (strcmp(sim_mode, "ramp") == 0) {
    latest_sample.temp_mC = generate_ramp_temp();
    }
    //mutex_unlock(&mode_lock);
    bool new_alert = (latest_sample.temp_mC < threshold);
    latest_sample.alert = new_alert;
    sample_ready = true;
    if (new_alert)
      alert_event = true;
    mutex_unlock(&sample_lock);
    wake_up_interruptible(&sample_wq);
    
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
    struct sample_record rec;
    struct sample s;

    // waits for a new sample
    if (wait_event_interruptible(sample_wq, sample_ready))
        return -ERESTARTSYS;

    mutex_lock(&sample_lock);

    s = latest_sample;

    // reset flags for next sample
    sample_ready = false;
    alert_event = false;

    mutex_unlock(&sample_lock);

    // armar struct binario
    rec.timestamp_jiffies = (u32)jiffies;
    rec.temp_mC = s.temp_mC;
    rec.alert = s.alert;
    memset(rec.reserved, 0, sizeof(rec.reserved));

    // copiar al espacio del usuario
    if (copy_to_user(buf, &rec, min(count, sizeof(rec))))
        return -EFAULT;

    return sizeof(rec);
}


static unsigned int nxp_simtemp_poll(struct file *file, poll_table *wait)
{
    unsigned int mask = 0;
    poll_wait(file, &sample_wq, wait);
    mutex_lock(&sample_lock);
    if (sample_ready)
      mask |= POLLIN | POLLRDNORM;  // nueva muestra disponible
    if (alert_event)
      mask |= POLLPRI;              // threshold crossing
    mutex_unlock(&sample_lock);

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
    if (ret) {
        pr_err("nxp_simtemp: misc_register failed\n");
        return ret;
    }

    // Crear sysfs files en orden
    ret = device_create_file(nxp_simtemp_dev.this_device, &dev_attr_threshold);
    if (ret) {
        pr_err("nxp_simtemp: failed to create sysfs file: threshold\n");
        goto err_remove;
    }

    ret = device_create_file(nxp_simtemp_dev.this_device, &dev_attr_sampling);
    if (ret) {
        pr_err("nxp_simtemp: failed to create sysfs file: sampling\n");
        goto err_remove_threshold;
    }

    ret = device_create_file(nxp_simtemp_dev.this_device, &dev_attr_stats);
    if (ret) {
        pr_err("nxp_simtemp: failed to create sysfs file: stats\n");
        goto err_remove_sampling;
    }

    ret = device_create_file(nxp_simtemp_dev.this_device, &dev_attr_mode);
    if (ret) {
        pr_err("nxp_simtemp: failed to create sysfs file: mode\n");
        goto err_remove_stats;
    }

    // Configurar timer
    timer_setup(&sample_timer, timer_callback, 0);
    mod_timer(&sample_timer, jiffies + msecs_to_jiffies(sampling_ms));
    pr_info("nxp_simtemp: timer armado con sampling %d ms\n", sampling_ms);

    // Inicializar primera muestra
    mutex_lock(&sample_lock);
    latest_sample.temp_mC = 20000; // valor inicial arbitrario
    latest_sample.alert = (latest_sample.temp_mC < threshold);
    sample_ready = true;
    alert_event = false;
    mutex_unlock(&sample_lock);

    pr_info("nxp_simtemp loaded\n");
    return 0;

err_remove_stats:
    device_remove_file(nxp_simtemp_dev.this_device, &dev_attr_stats);
err_remove_sampling:
    device_remove_file(nxp_simtemp_dev.this_device, &dev_attr_sampling);
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
    device_remove_file(nxp_simtemp_dev.this_device, &dev_attr_mode);
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

