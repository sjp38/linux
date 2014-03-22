/*
 * lib/smp_processor_id.c
 *
 * DEBUG_PREEMPT variant of smp_processor_id().
 */
#include <linux/export.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>

notrace static unsigned int check_preemption_disabled(char *what)
{
	int this_cpu = raw_smp_processor_id();

	if (likely(preempt_count()))
		goto out;

	if (irqs_disabled())
		goto out;

	/*
	 * Kernel threads bound to a single CPU can safely use
	 * smp_processor_id():
	 */
	if (cpumask_equal(tsk_cpus_allowed(current), cpumask_of(this_cpu)))
		goto out;

	/*
	 * It is valid to assume CPU-locality during early bootup:
	 */
	if (system_state != SYSTEM_RUNNING)
		goto out;

	/*
	 * Avoid recursion:
	 */
	preempt_disable_notrace();

	if (!printk_ratelimit())
		goto out_enable;

	printk(KERN_ERR "BUG: using %s in preemptible [%08x] code: %s/%d\n",
		what, preempt_count() - 1, current->comm, current->pid);

	print_symbol("caller is %s\n", (long)__builtin_return_address(0));
	dump_stack();

out_enable:
	preempt_enable_no_resched_notrace();
out:
	return this_cpu;
}

notrace unsigned int debug_smp_processor_id(void)
{
	return check_preemption_disabled("smp_processor_id()");
}
EXPORT_SYMBOL(debug_smp_processor_id);

notrace void __this_cpu_preempt_check(const char *op)
{
	char text[40];

	snprintf(text, sizeof(text), "__this_cpu_%s()", op);
	check_preemption_disabled(text);
}
EXPORT_SYMBOL(__this_cpu_preempt_check);
