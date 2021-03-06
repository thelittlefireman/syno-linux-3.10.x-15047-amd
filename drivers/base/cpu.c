#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
 
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/topology.h>
#include <linux/device.h>
#include <linux/node.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/percpu.h>

#include "base.h"

#if defined(MY_ABC_HERE)
#include <linux/synobios.h>
#endif  

struct bus_type cpu_subsys = {
	.name = "cpu",
	.dev_name = "cpu",
};
EXPORT_SYMBOL_GPL(cpu_subsys);

static DEFINE_PER_CPU(struct device *, cpu_sys_devices);

#ifdef CONFIG_HOTPLUG_CPU
static void change_cpu_under_node(struct cpu *cpu,
			unsigned int from_nid, unsigned int to_nid)
{
	int cpuid = cpu->dev.id;
	unregister_cpu_under_node(cpuid, from_nid);
	register_cpu_under_node(cpuid, to_nid);
	cpu->node_id = to_nid;
}

static ssize_t show_online(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, dev);

	return sprintf(buf, "%u\n", !!cpu_online(cpu->dev.id));
}

static ssize_t __ref store_online(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct cpu *cpu = container_of(dev, struct cpu, dev);
	int cpuid = cpu->dev.id;
	int from_nid, to_nid;
	ssize_t ret;

#ifdef MY_ABC_HERE
	if(syno_is_hw_version(HW_DS712pv20)) {
		if( 1 == cpuid || 3 == cpuid ) {
			printk(KERN_ERR "This model does not allow changing the specified cpu state.\n");
			ret = count;
			goto END;
		}
	}
#endif  

	cpu_hotplug_driver_lock();
	switch (buf[0]) {
	case '0':
		ret = cpu_down(cpuid);
		if (!ret)
			kobject_uevent(&dev->kobj, KOBJ_OFFLINE);
		break;
	case '1':
		from_nid = cpu_to_node(cpuid);
		ret = cpu_up(cpuid);

		to_nid = cpu_to_node(cpuid);
		if (from_nid != to_nid)
			change_cpu_under_node(cpu, from_nid, to_nid);

		if (!ret)
			kobject_uevent(&dev->kobj, KOBJ_ONLINE);
		break;
	default:
		ret = -EINVAL;
	}
	cpu_hotplug_driver_unlock();

	if (ret >= 0)
		ret = count;

#ifdef MY_ABC_HERE
END:
#endif  

	return ret;
}
static DEVICE_ATTR(online, 0644, show_online, store_online);

static void __cpuinit register_cpu_control(struct cpu *cpu)
{
	device_create_file(&cpu->dev, &dev_attr_online);
}
void unregister_cpu(struct cpu *cpu)
{
	int logical_cpu = cpu->dev.id;

	unregister_cpu_under_node(logical_cpu, cpu_to_node(logical_cpu));

	device_remove_file(&cpu->dev, &dev_attr_online);

	device_unregister(&cpu->dev);
	per_cpu(cpu_sys_devices, logical_cpu) = NULL;
	return;
}

#ifdef CONFIG_ARCH_CPU_PROBE_RELEASE
static ssize_t cpu_probe_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t count)
{
	return arch_cpu_probe(buf, count);
}

static ssize_t cpu_release_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t count)
{
	return arch_cpu_release(buf, count);
}

static DEVICE_ATTR(probe, S_IWUSR, NULL, cpu_probe_store);
static DEVICE_ATTR(release, S_IWUSR, NULL, cpu_release_store);
#endif  

#else  
static inline void register_cpu_control(struct cpu *cpu)
{
}
#endif  

#ifdef CONFIG_KEXEC
#include <linux/kexec.h>

static ssize_t show_crash_notes(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, dev);
	ssize_t rc;
	unsigned long long addr;
	int cpunum;

	cpunum = cpu->dev.id;

	addr = per_cpu_ptr_to_phys(per_cpu_ptr(crash_notes, cpunum));
	rc = sprintf(buf, "%Lx\n", addr);
	return rc;
}
static DEVICE_ATTR(crash_notes, 0400, show_crash_notes, NULL);

static ssize_t show_crash_notes_size(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	ssize_t rc;

	rc = sprintf(buf, "%zu\n", sizeof(note_buf_t));
	return rc;
}
static DEVICE_ATTR(crash_notes_size, 0400, show_crash_notes_size, NULL);
#endif

struct cpu_attr {
	struct device_attribute attr;
	const struct cpumask *const * const map;
};

static ssize_t show_cpus_attr(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct cpu_attr *ca = container_of(attr, struct cpu_attr, attr);
	int n = cpulist_scnprintf(buf, PAGE_SIZE-2, *(ca->map));

	buf[n++] = '\n';
	buf[n] = '\0';
	return n;
}

#define _CPU_ATTR(name, map) \
	{ __ATTR(name, 0444, show_cpus_attr, NULL), map }

static struct cpu_attr cpu_attrs[] = {
	_CPU_ATTR(online, &cpu_online_mask),
	_CPU_ATTR(possible, &cpu_possible_mask),
	_CPU_ATTR(present, &cpu_present_mask),
};

static ssize_t print_cpus_kernel_max(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int n = snprintf(buf, PAGE_SIZE-2, "%d\n", NR_CPUS - 1);
	return n;
}
static DEVICE_ATTR(kernel_max, 0444, print_cpus_kernel_max, NULL);

unsigned int total_cpus;

static ssize_t print_cpus_offline(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int n = 0, len = PAGE_SIZE-2;
	cpumask_var_t offline;

	if (!alloc_cpumask_var(&offline, GFP_KERNEL))
		return -ENOMEM;
	cpumask_andnot(offline, cpu_possible_mask, cpu_online_mask);
	n = cpulist_scnprintf(buf, len, offline);
	free_cpumask_var(offline);

	if (total_cpus && nr_cpu_ids < total_cpus) {
		if (n && n < len)
			buf[n++] = ',';

		if (nr_cpu_ids == total_cpus-1)
			n += snprintf(&buf[n], len - n, "%d", nr_cpu_ids);
		else
			n += snprintf(&buf[n], len - n, "%d-%d",
						      nr_cpu_ids, total_cpus-1);
	}

	n += snprintf(&buf[n], len - n, "\n");
	return n;
}
static DEVICE_ATTR(offline, 0444, print_cpus_offline, NULL);

static void cpu_device_release(struct device *dev)
{
	 
}

int __cpuinit register_cpu(struct cpu *cpu, int num)
{
	int error;

	cpu->node_id = cpu_to_node(num);
	memset(&cpu->dev, 0x00, sizeof(struct device));
	cpu->dev.id = num;
	cpu->dev.bus = &cpu_subsys;
	cpu->dev.release = cpu_device_release;
#ifdef CONFIG_ARCH_HAS_CPU_AUTOPROBE
	cpu->dev.bus->uevent = arch_cpu_uevent;
#endif
	error = device_register(&cpu->dev);
	if (!error && cpu->hotpluggable)
		register_cpu_control(cpu);
	if (!error)
		per_cpu(cpu_sys_devices, num) = &cpu->dev;
	if (!error)
		register_cpu_under_node(num, cpu_to_node(num));

#ifdef CONFIG_KEXEC
	if (!error)
		error = device_create_file(&cpu->dev, &dev_attr_crash_notes);
	if (!error)
		error = device_create_file(&cpu->dev,
					   &dev_attr_crash_notes_size);
#endif
	return error;
}

struct device *get_cpu_device(unsigned cpu)
{
	if (cpu < nr_cpu_ids && cpu_possible(cpu))
		return per_cpu(cpu_sys_devices, cpu);
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(get_cpu_device);

#ifdef CONFIG_ARCH_HAS_CPU_AUTOPROBE
static DEVICE_ATTR(modalias, 0444, arch_print_cpu_modalias, NULL);
#endif

static struct attribute *cpu_root_attrs[] = {
#ifdef CONFIG_ARCH_CPU_PROBE_RELEASE
	&dev_attr_probe.attr,
	&dev_attr_release.attr,
#endif
	&cpu_attrs[0].attr.attr,
	&cpu_attrs[1].attr.attr,
	&cpu_attrs[2].attr.attr,
	&dev_attr_kernel_max.attr,
	&dev_attr_offline.attr,
#ifdef CONFIG_ARCH_HAS_CPU_AUTOPROBE
	&dev_attr_modalias.attr,
#endif
	NULL
};

static struct attribute_group cpu_root_attr_group = {
	.attrs = cpu_root_attrs,
};

static const struct attribute_group *cpu_root_attr_groups[] = {
	&cpu_root_attr_group,
	NULL,
};

bool cpu_is_hotpluggable(unsigned cpu)
{
	struct device *dev = get_cpu_device(cpu);
	return dev && container_of(dev, struct cpu, dev)->hotpluggable;
}
EXPORT_SYMBOL_GPL(cpu_is_hotpluggable);

#ifdef CONFIG_GENERIC_CPU_DEVICES
static DEFINE_PER_CPU(struct cpu, cpu_devices);
#endif

static void __init cpu_dev_register_generic(void)
{
#ifdef CONFIG_GENERIC_CPU_DEVICES
	int i;

	for_each_possible_cpu(i) {
		if (register_cpu(&per_cpu(cpu_devices, i), i))
			panic("Failed to register CPU device");
	}
#endif
}

void __init cpu_dev_init(void)
{
	if (subsys_system_register(&cpu_subsys, cpu_root_attr_groups))
		panic("Failed to register CPU subsystem");

	cpu_dev_register_generic();
}
