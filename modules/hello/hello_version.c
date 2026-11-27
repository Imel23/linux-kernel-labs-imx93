// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/utsname.h>

static char *who = "Imel23";
module_param(who, charp, 0644);

static int __init hello_init(void)
{
	pr_alert("Hello %s. You are currently using Linux %s.\n", who, init_utsname()->release);
	return 0;
}

static void __exit hello_exit(void)
{
	time64_t uptime = ktime_get_seconds();

	pr_alert("Goodbye, %s! You used the system for %lld seconds.\n", who, (long long)uptime);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Imel23");
MODULE_DESCRIPTION("A simple Hello World kernel module");
module_init(hello_init);
module_exit(hello_exit);
