#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <linux/device.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/delay.h>

#include <linux/workqueue.h>

MODULE_LICENSE("GPL");  


#define THREAD_NUM 100


struct thr_priv {
	int num;
	atomic_t state;
	struct list_head list_entry;
};

struct task_struct *client[THREAD_NUM];
struct task_struct *server;
struct thr_priv *priv[THREAD_NUM];
static spinlock_t listlock;
struct list_head scst_sess_shut_list;
wait_queue_head_t scst_mgmt_waitQ;



int thread_client(void *data)
{
	struct thr_priv *priv = (struct thr_priv *)data;
	unsigned long flags;

        printk(KERN_ERR "start client-%d\n", priv->num);
	/* set_current_state(TASK_INTERRUPTIBLE); */

	spin_lock_irqsave(&listlock, flags);
	list_add_tail(&priv->list_entry, &scst_sess_shut_list);
	spin_unlock_irqrestore(&listlock, flags);

	smp_rmb();
	while ((int)atomic_read(&priv->state) < 10) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(DIV_ROUND_UP(HZ, 10));
	}

	//set_current_state(TASK_RUNNING);
		/* set_current_state(TASK_INTERRUPTIBLE); */

	while (!kthread_should_stop()) {
		schedule_timeout(DIV_ROUND_UP(HZ, 10));
		set_current_state(TASK_INTERRUPTIBLE);

	}

        printk(KERN_ERR "finish client-%d\n", priv->num);
	return 0;
}

int thread_server(void *data)
{
	int sum = 0;
	DEFINE_WAIT(__wait);
	struct list_head *h;

	printk(KERN_ERR "start server\n");
	
	while (!kthread_should_stop()) {
		__wait.flags |= WQ_FLAG_EXCLUSIVE;
		__add_wait_queue(&scst_mgmt_waitQ, &__wait);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		
		printk(KERN_ERR "wake-up\n");

		list_for_each(h, &scst_sess_shut_list) {
			struct thr_priv *pv = list_entry(h, struct thr_priv, list_entry);
			sum += pv->num;
		}
		printk(KERN_ERR "sum=%d\n", sum);

		list_for_each(h, &scst_sess_shut_list) {
			struct thr_priv *pv = list_entry(h, struct thr_priv, list_entry);
			atomic_set(&pv->state, 10);
			smp_wmb();
		}

		msleep(5000);
		printk(KERN_ERR "start killing clients\n");
		list_for_each(h, &scst_sess_shut_list) {
			struct thr_priv *pv = list_entry(h, struct thr_priv, list_entry);
			int i = pv->num;
			kthread_stop(client[i]);
			kfree(priv[i]);
			smp_wmb();
		}
		printk(KERN_ERR "finish killing clients\n");
	}

	printk(KERN_ERR "finish server\n");
	return 0;
}

static int __init hello_init(void)
{
	int i;
	
	spin_lock_init(&listlock);
	INIT_LIST_HEAD(&scst_sess_shut_list);
	init_waitqueue_head(&scst_mgmt_waitQ);


	/* server = kthread_run(&thread_server, */
	/* 		     NULL, */
	/* 		     "server"); */
	
	for (i = 0; i < THREAD_NUM; i++) {
		priv[i] = kmalloc(sizeof(struct thr_priv), GFP_KERNEL);
		priv[i]->num = i;
		atomic_set(&priv[i]->state, 1);
		client[i] = kthread_run(&thread_client,
				      priv[i],
				      "client-%d",
				      i);
	}

	msleep(1000); // time to complete clients
	
	// wake up mgmt after creating all clients
	//wake_up(&scst_mgmt_waitQ);

	printk(KERN_EMERG "==========READY\n\n");
	return 0;
}

static void __exit hello_exit(void)
{
	/* for (i = 0; i < THREAD_NUM; i++) { */
	/* 	kthread_stop(client[i]); */
	/* 	kfree(priv[i]); */
	/* } */
	{
		struct list_head *h;
		printk(KERN_EMERG "start setting state\n");
		list_for_each(h, &scst_sess_shut_list) {
			struct thr_priv *pv = list_entry(h, struct thr_priv, list_entry);
			atomic_set(&pv->state, 10);
			smp_wmb();
		}
		printk(KERN_EMERG "finish setting state\n");
		msleep(5000);
		printk(KERN_EMERG "start killing clients\n");
		list_for_each(h, &scst_sess_shut_list) {
			struct thr_priv *pv = list_entry(h, struct thr_priv, list_entry);
			int i = pv->num;
			kthread_stop(client[i]);
			kfree(priv[i]);
			smp_wmb();
		}
	}
	
	//kthread_stop(server);
	printk(KERN_EMERG "==============Goodbye from the LKM!\n\n\n");
}




module_init(hello_init);
module_exit(hello_exit);
