#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <linux/uidgid.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/wait.h>

#define BUF_SIZE 2048

DECLARE_WAIT_QUEUE_HEAD(wq);

static unsigned int major;
static struct cdev *c_dev;

static int rbuf_open(struct inode *, struct file *);
static int rbuf_write(struct file *, const char __user *, size_t, loff_t *);
static int rbuf_read(struct file *, char __user *, size_t, loff_t *);
static int rbuf_release(struct inode *, struct file *);

static const struct file_operations fops = {
	.open = rbuf_open,
	.write = rbuf_write,
	.read = rbuf_read,
	.release = rbuf_release
};

static struct fbuffer {
	uid_t owner;
	char data[BUF_SIZE];
	int head;
	int tail;
	int counter;
} *table;

static unsigned int users_count;

static int get_usr_ind(void)
{
	int i;
	uid_t current_user = get_current_user()->uid;

	if (table == NULL)
		return -2;
	for (i = 0; i < users_count; i++)
		if (table[i].owner == current_user)
			return i;
	return -1;
}

static bool write_cond(int i)
{
	if (table[i].counter < BUF_SIZE)
		return true;
	return false;
}

static bool read_cond(int i)
{
	if (table[i].counter > 0)
		return true;
	return false;
}

static int rbuf_open(struct inode *inode, struct file *filp)
{
	uid_t current_user = get_current_user()->uid;

	pr_warn("Device opened by process with PID: %d PPID: %d",
		current->pid, current->real_parent->pid);
	pr_warn("UID: %d", current_user);
	if (!table) {
		int i;

		pr_warn("Creating initial table...");
		table = kzalloc(sizeof(*table), GFP_KERNEL);
		table->owner = current_user;
		table->head = 0;
		table->tail = 0;
		table->counter = 0;
		users_count++;
		pr_warn("Table creation complete.");
	} else {
		if (get_usr_ind() == -1) {
			int i;
			struct fbuffer *temp;

			pr_warn("Creating buffer for new user...");
			temp = table;
			table = krealloc(temp, sizeof(struct fbuffer) *
					 (users_count + 1), GFP_KERNEL);
			temp = NULL;
			table[users_count].owner = current_user;
			table[users_count].head = 0;
			table[users_count].tail = 0;
			table[users_count].counter = 0;
			users_count++;
			pr_warn("Buffer creating complete.");
		}
	}
	return 0;
}

static int rbuf_write(struct file *filp, const char __user *usr_buf,
		      size_t count, loff_t *f_pos)
{
	int b_write = 0;
	char local_buf;
	int i = get_usr_ind();

	pr_alert("User %d request write %d bytes.",
		 table[i].owner, count);
	while (b_write < count) {
		if (!write_cond(i))
			wait_event_interruptible(wq, write_cond(i));
		while (write_cond(i) && b_write < count) {
			unsigned long copy_retval;

			copy_retval = copy_from_user(&local_buf,
						     usr_buf + b_write, 1);
			if (copy_retval != 0) {
				pr_err("Error: copy_from_user() function.");
				return -1;
			}
			table[i].data[table[i].head] = local_buf;
			table[i].head++;
			table[i].counter++;
			if (table[i].head >= BUF_SIZE)
				table[i].head = 0;
			b_write++;
			wake_up(&wq);
		}
	}
	return b_write;
}

static int rbuf_read(struct file *filp, char __user *usr_buf,
		     size_t count, loff_t *f_pos)
{
	int b_read = 0;
	char local_buf;
	int i = get_usr_ind();

	pr_alert("User %d request read %d bytes.",
		 table[i].owner, count);
	while (b_read < count) {
		if (!read_cond(i))
			wait_event_interruptible(wq, read_cond(i));
		while (read_cond(i) && b_read < count) {
			unsigned long copy_retval;

			local_buf = table[i].data[table[i].tail];
			table[i].data[table[i].tail] = 0;
			table[i].tail++;
			table[i].counter--;
			if (table[i].tail >= BUF_SIZE)
				table[i].tail = 0;
			copy_retval = copy_to_user(usr_buf + b_read,
						   &local_buf, 1);
			if (copy_retval != 0) {
				pr_err("Error: copy_to_user() function.");
				return -1;
			}
			b_read++;
			wake_up(&wq);
		}
	}
	return b_read;
}

static int rbuf_release(struct inode *inode, struct file *filp)
{
	pr_alert("Device released by process with PID: %d PPID: %d",
		 current->pid, current->real_parent->pid);
	wake_up(&wq);
	return 0;
}

static int __init rbuf_init(void)
{
	int ret;
	dev_t dev_number;

	c_dev = cdev_alloc();
	c_dev->owner = THIS_MODULE;
	ret = alloc_chrdev_region(&dev_number, 0, 1, "ring_buffer");
	if (ret < 0) {
		pr_err("Char device region allocation failed.");
		return ret;
	}
	major = MAJOR(dev_number);
	cdev_init(c_dev, &fops);
	ret = cdev_add(c_dev, dev_number, 1);
	if (ret < 0) {
		unregister_chrdev_region(MKDEV(major, 0), 1);
		pr_err("Major number allocation failed.");
		return ret;
	}
	pr_warn("---======= ring_buffer module installed =======---");
	pr_warn("Major = %d\tMinor = %d", MAJOR(dev_number), MINOR(dev_number));
	return 0;
}

static void __exit rbuf_exit(void)
{
	kfree(table);
	cdev_del(c_dev);
	unregister_chrdev_region(MKDEV(major, 0), 1);
	pr_warn("---======= ring_buffer module removed =======---");
}

module_init(rbuf_init);
module_exit(rbuf_exit);
MODULE_LICENSE("GPL");
