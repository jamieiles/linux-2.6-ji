/*
 *    Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corp.
 *			 <benh@kernel.crashing.org>
 *    and		 Arnd Bergmann, IBM Corp.
 *    Merged from powerpc/kernel/of_platform.c and
 *    sparc{,64}/kernel/of_device.c by Stephen Rothwell
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/amba/bus.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

const struct of_device_id of_default_bus_match_table[] = {
	{ .compatible = "simple-bus", },
#ifdef CONFIG_ARM_AMBA
	{ .compatible = "arm,amba-bus", },
#endif /* CONFIG_ARM_AMBA */
	{} /* Empty terminated list */
};

static int of_dev_node_match(struct device *dev, void *data)
{
	return dev->of_node == data;
}

/**
 * of_find_device_by_node - Find the platform_device associated with a node
 * @np: Pointer to device tree node
 *
 * Returns platform_device pointer, or NULL if not found
 */
struct platform_device *of_find_device_by_node(struct device_node *np)
{
	struct device *dev;

	dev = bus_find_device(&platform_bus_type, NULL, np, of_dev_node_match);
	return dev ? to_platform_device(dev) : NULL;
}
EXPORT_SYMBOL(of_find_device_by_node);

#if defined(CONFIG_PPC_DCR)
#include <asm/dcr.h>
#endif

#if !defined(CONFIG_SPARC)
/*
 * The following routines scan a subtree and registers a device for
 * each applicable node.
 *
 * Note: sparc doesn't use these routines because it has a different
 * mechanism for creating devices from device tree nodes.
 */

/**
 * of_device_make_bus_id - Use the device node data to assign a unique name
 * @dev: pointer to device structure that is linked to a device tree node
 *
 * This routine will first try using either the dcr-reg or the reg property
 * value to derive a unique name.  As a last resort it will use the node
 * name followed by a unique number.
 */
void of_device_make_bus_id(struct device *dev)
{
	static atomic_t bus_no_reg_magic;
	struct device_node *node = dev->of_node;
	const u32 *reg;
	u64 addr;
	int magic;

#ifdef CONFIG_PPC_DCR
	/*
	 * If it's a DCR based device, use 'd' for native DCRs
	 * and 'D' for MMIO DCRs.
	 */
	reg = of_get_property(node, "dcr-reg", NULL);
	if (reg) {
#ifdef CONFIG_PPC_DCR_NATIVE
		dev_set_name(dev, "d%x.%s", *reg, node->name);
#else /* CONFIG_PPC_DCR_NATIVE */
		u64 addr = of_translate_dcr_address(node, *reg, NULL);
		if (addr != OF_BAD_ADDR) {
			dev_set_name(dev, "D%llx.%s",
				     (unsigned long long)addr, node->name);
			return;
		}
#endif /* !CONFIG_PPC_DCR_NATIVE */
	}
#endif /* CONFIG_PPC_DCR */

	/*
	 * For MMIO, get the physical address
	 */
	reg = of_get_property(node, "reg", NULL);
	if (reg) {
		addr = of_translate_address(node, reg);
		if (addr != OF_BAD_ADDR) {
			dev_set_name(dev, "%llx.%s",
				     (unsigned long long)addr, node->name);
			return;
		}
	}

	/*
	 * No BusID, use the node name and add a globally incremented
	 * counter (and pray...)
	 */
	magic = atomic_add_return(1, &bus_no_reg_magic);
	dev_set_name(dev, "%s.%d", node->name, magic - 1);
}

/**
 * of_device_alloc - Allocate and initialize an of_device
 * @np: device node to assign to device
 * @bus_id: Name to assign to the device.  May be null to use default name.
 * @parent: Parent device.
 */
struct platform_device *of_device_alloc(struct device_node *np,
				  const char *bus_id,
				  struct device *parent)
{
	struct platform_device *dev;
	int rc, i, num_reg = 0, num_irq;
	struct resource *res, temp_res;

	dev = platform_device_alloc("", -1);
	if (!dev)
		return NULL;

	/* count the io and irq resources */
	while (of_address_to_resource(np, num_reg, &temp_res) == 0)
		num_reg++;
	num_irq = of_irq_count(np);

	/* Populate the resource table */
	if (num_irq || num_reg) {
		res = kzalloc(sizeof(*res) * (num_irq + num_reg), GFP_KERNEL);
		if (!res) {
			platform_device_put(dev);
			return NULL;
		}

		dev->num_resources = num_reg + num_irq;
		dev->resource = res;
		for (i = 0; i < num_reg; i++, res++) {
			rc = of_address_to_resource(np, i, res);
			WARN_ON(rc);
		}
		WARN_ON(of_irq_to_resource_table(np, res, num_irq) != num_irq);
	}

	dev->dev.of_node = of_node_get(np);
#if defined(CONFIG_MICROBLAZE)
	dev->dev.dma_mask = &dev->archdata.dma_mask;
#endif
	dev->dev.parent = parent;

	if (bus_id)
		dev_set_name(&dev->dev, "%s", bus_id);
	else
		of_device_make_bus_id(&dev->dev);

	return dev;
}
EXPORT_SYMBOL(of_device_alloc);

/**
 * of_platform_device_create_pdata - Alloc, initialize and register an of_device
 * @np: pointer to node to create device for
 * @bus_id: name to assign device
 * @platform_data: pointer to populate platform_data pointer with
 * @parent: Linux device model parent device.
 *
 * Returns pointer to created platform device, or NULL if a device was not
 * registered.  Unavailable devices will not get registered.
 */
struct platform_device *of_platform_device_create_pdata(
					struct device_node *np,
					const char *bus_id,
					void *platform_data,
					struct device *parent)
{
	struct platform_device *dev;

	if (!of_device_is_available(np))
		return NULL;

	dev = of_device_alloc(np, bus_id, parent);
	if (!dev)
		return NULL;

#if defined(CONFIG_MICROBLAZE)
	dev->archdata.dma_mask = 0xffffffffUL;
#endif
	dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	dev->dev.bus = &platform_bus_type;
	dev->dev.platform_data = platform_data;

	/* We do not fill the DMA ops for platform devices by default.
	 * This is currently the responsibility of the platform code
	 * to do such, possibly using a device notifier
	 */

	if (of_device_add(dev) != 0) {
		platform_device_put(dev);
		return NULL;
	}

	return dev;
}

/**
 * of_platform_device_create - Alloc, initialize and register an of_device
 * @np: pointer to node to create device for
 * @bus_id: name to assign device
 * @parent: Linux device model parent device.
 *
 * Returns pointer to created platform device, or NULL if a device was not
 * registered.  Unavailable devices will not get registered.
 */
struct platform_device *of_platform_device_create(struct device_node *np,
					    const char *bus_id,
					    struct device *parent)
{
	return of_platform_device_create_pdata(np, bus_id, NULL, parent);
}
EXPORT_SYMBOL(of_platform_device_create);

struct of_platform_prepare_data {
	struct list_head list;
	struct device_node *node;
	struct device *dev;		/* assigned device */

	int num_resources;
	struct resource resource[0];
};

static LIST_HEAD(of_platform_prepare_list);
static struct notifier_block of_platform_nb;

static struct of_platform_prepare_data *of_platform_find_prepare_data(
						struct device_node *node)
{
	struct of_platform_prepare_data *prep;
	list_for_each_entry(prep, &of_platform_prepare_list, list)
		if (prep->node == node)
			return prep;
	return NULL;
}

static bool of_pdev_match_resources(struct platform_device *pdev,
			struct of_platform_prepare_data *prep)
{
	struct resource *node_res = prep->resource;
	struct resource *pdev_res;
	int i, j;

	if (prep->num_resources == 0 || pdev->num_resources == 0)
		return false;

	dev_dbg(&pdev->dev, "compare dt node %s\n", prep->node->full_name);

	/* Compare both resource tables and make sure every node resource
	 * is represented by the platform device.  Here we check that each
	 * resource has corresponding entry with the same type and start
	 * values, and the end value falls inside the range specified
	 * in the device tree node. */
	for (i = 0; i < prep->num_resources; i++, node_res++) {
		pr_debug("        node res %2i:%.8x..%.8x[%lx]...\n", i,
			node_res->start, node_res->end, node_res->flags);
		pdev_res = pdev->resource;
		for (j = 0; j < pdev->num_resources; j++, pdev_res++) {
			pr_debug("        pdev res %2i:%.8x..%.8x[%lx]\n", j,
				pdev_res->start, pdev_res->end, pdev_res->flags);
			if ((pdev_res->start == node_res->start) &&
			    (pdev_res->end >= node_res->start) &&
			    (pdev_res->end <= node_res->end) &&
			    (pdev_res->flags == node_res->flags)) {
				pr_debug("    ...MATCH!  :-)\n");
				break;
			}
		}
		if (j >= pdev->num_resources)
			return false;
	}
	return true;
}

static int of_platform_device_notifier_call(struct notifier_block *nb,
					unsigned long event, void *_dev)
{
	struct platform_device *pdev = to_platform_device(_dev);
	struct of_platform_prepare_data *prep;

	switch (event) {
	case BUS_NOTIFY_ADD_DEVICE:
		if (pdev->dev.of_node)
			return NOTIFY_DONE;

		list_for_each_entry(prep, &of_platform_prepare_list, list) {
			if (prep->dev)
				continue;

			if (!of_pdev_match_resources(pdev, prep))
				continue;

			/* If disabled, don't let the device bind */
			if (!of_device_is_available(prep->node)) {
				char buf[strlen(pdev->name) + 12];
				dev_info(&pdev->dev, "disabled by dt node %s\n",
					prep->node->full_name);
				sprintf(buf, "%s-disabled", pdev->name);
				pdev->name = kstrdup(buf, GFP_KERNEL);
				continue;
			}

			dev_info(&pdev->dev, "attaching dt node %s\n",
				prep->node->full_name);
			prep->dev = get_device(&pdev->dev);
			pdev->dev.of_node = of_node_get(prep->node);
			return NOTIFY_OK;
		}
		break;

	case BUS_NOTIFY_DEL_DEVICE:
		list_for_each_entry(prep, &of_platform_prepare_list, list) {
			if (prep->dev == &pdev->dev) {
				dev_info(&pdev->dev, "detaching dt node %s\n",
					 prep->node->full_name);
				of_node_put(pdev->dev.of_node);
				put_device(prep->dev);
				pdev->dev.of_node = NULL;
				prep->dev = NULL;
				return NOTIFY_OK;
			}
		}
		break;
	}

	return NOTIFY_DONE;
}

/**
 * of_platform_prepare - Flag nodes to be used for creating devices
 * @root: parent of the first level to probe or NULL for the root of the tree
 * @bus_match: match table for child bus nodes, or NULL
 *
 * This function sets up 'snooping' of device tree registrations and
 * when a device registration is found that matches a node in the
 * device tree, it populates the platform_device with a pointer to the
 * matching node.
 *
 * A bus notifier is used to implement this behaviour.  When this
 * function is called, it will parse all the child nodes of @root and
 * create a lookup table of eligible device nodes.  A device node is
 * considered eligible if it:
 *    a) has a compatible property,
 *    b) has memory mapped registers, and
 *    c) has a mappable interrupt.
 *
 * It will also recursively parse child buses providing
 *    a) the child bus node has a ranges property (children have
 *       memory-mapped registers), and
 *    b) it is compatible with the @matches list.
 *
 * The lookup table will be used as data for a platform bus notifier
 * that will compare each new device registration with the table
 * before a device driver is bound to it.  If there is a match, then
 * the of_node pointer will be added to the device.  Therefore it is
 * important to call this function *before* any platform devices get
 * registered.
 */
void of_platform_prepare(struct device_node *root,
			 const struct of_device_id *matches)
{
	struct device_node *child;
	struct of_platform_prepare_data *prep;

	/* register the notifier if it isn't already */
	if (!of_platform_nb.notifier_call) {
		of_platform_nb.notifier_call = of_platform_device_notifier_call;
		bus_register_notifier(&platform_bus_type, &of_platform_nb);
	}

	/* If root is null, then start at the root of the tree */
	root = root ? of_node_get(root) : of_find_node_by_path("/");
	if (!root)
		return;

	pr_debug("of_platform_prepare()\n");
	pr_debug(" starting at: %s\n", root->full_name);

	/* Loop over children and record the details */
	for_each_child_of_node(root, child) {
		struct resource *res;
		int num_irq, num_reg, i;

		/* If this is a bus node, recursively inspect the children,
		 * but *don't* prepare it.  Prepare only concerns
		 * itself with leaf-nodes.  */
		if (of_match_node(matches, child)) {
			of_platform_prepare(child, matches);
			continue;
		}

		/* Is it already in the list? */
		if (of_platform_find_prepare_data(child))
			continue;

		/* Make sure it has a compatible property */
		if (!of_get_property(child, "compatible", NULL))
			continue;

		/*
		 * Count the resources.  If the device doesn't have any
		 * register ranges, then it gets skipped because there is no
		 * way to match such a device against static registration
		 */
		num_irq = of_irq_count(child);
		num_reg = of_address_count(child);
		if (!num_reg)
			continue;

		/* Device node looks valid; record the details */
		prep = kzalloc(sizeof(*prep) +
			(sizeof(prep->resource[0]) * (num_irq + num_reg)),
			GFP_KERNEL);
		if (!prep)
			return; /* We're screwed if malloc doesn't work. */

		INIT_LIST_HEAD(&prep->list);

		res = &prep->resource[0];
		for (i = 0; i < num_reg; i++, res++)
			WARN_ON(of_address_to_resource(child, i, res));
		WARN_ON(of_irq_to_resource_table(child, res, num_irq) != num_irq);
		prep->num_resources = num_reg + num_irq;
		prep->node = of_node_get(child);

		list_add_tail(&prep->list, &of_platform_prepare_list);

		pr_debug("%s() - %s prepared (%i regs, %i irqs)\n",
			__func__, prep->node->full_name, num_reg, num_irq);
	}
}

#ifdef CONFIG_ARM_AMBA
static struct amba_device *of_amba_device_create(struct device_node *node,
						 const char *bus_id,
						 void *platform_data,
						 struct device *parent)
{
	struct amba_device *dev;
	const void *prop;
	int i, ret;

	pr_debug("Creating amba device %s\n", node->full_name);

	if (!of_device_is_available(node))
		return NULL;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	/* setup generic device info */
	dev->dev.coherent_dma_mask = ~0;
	dev->dev.of_node = of_node_get(node);
	dev->dev.parent = parent;
	dev->dev.platform_data = platform_data;
	if (bus_id)
		dev_set_name(&dev->dev, "%s", bus_id);
	else
		of_device_make_bus_id(&dev->dev);

	/* setup amba-specific device info */
	dev->dma_mask = ~0;

	/* Allow the HW Peripheral ID to be overridden */
	prop = of_get_property(node, "arm,primecell-periphid", NULL);
	if (prop)
		dev->periphid = of_read_ulong(prop, 1);

	/* Decode the IRQs and address ranges */
	for (i = 0; i < AMBA_NR_IRQS; i++)
		dev->irq[i] = irq_of_parse_and_map(node, i);

	ret = of_address_to_resource(node, 0, &dev->res);
	if (ret)
		goto err_free;

	ret = amba_device_register(dev, &iomem_resource);
	if (ret)
		goto err_free;

	return dev;

err_free:
	kfree(dev);
	return NULL;
}
#else /* CONFIG_ARM_AMBA */
static struct amba_device *of_amba_device_create(struct device_node *node,
						 const char *bus_id,
						 void *platform_data,
						 struct device *parent)
{
	return NULL;
}
#endif /* CONFIG_ARM_AMBA */

/**
 * of_devname_lookup() - Given a device node, lookup the preferred Linux name
 */
static const struct of_dev_auxdata *of_dev_lookup(const struct of_dev_auxdata *lookup,
				 struct device_node *np)
{
	struct resource res;
	if (lookup) {
		for(; lookup->name != NULL; lookup++) {
			if (!of_device_is_compatible(np, lookup->compatible))
				continue;
			if (of_address_to_resource(np, 0, &res))
				continue;
			if (res.start != lookup->phys_addr)
				continue;
			pr_debug("%s: devname=%s\n", np->full_name, lookup->name);
			return lookup;
		}
	}
	return NULL;
}

/**
 * of_platform_bus_create() - Create a device for a node and its children.
 * @bus: device node of the bus to instantiate
 * @matches: match table for bus nodes
 * disallow recursive creation of child buses
 * @parent: parent for new device, or NULL for top level.
 *
 * Creates a platform_device for the provided device_node, and optionally
 * recursively create devices for all the child nodes.
 */
static int of_platform_bus_create(struct device_node *bus,
				  const struct of_device_id *matches,
				  const struct of_dev_auxdata *lookup,
				  struct device *parent, bool strict)
{
	const struct of_dev_auxdata *auxdata;
	struct of_platform_prepare_data *prep;
	struct device_node *child;
	struct platform_device *dev;
	const char *bus_id = NULL;
	void *platform_data = NULL;
	int id = -1;
	int rc = 0;

	/* Make sure it has a compatible property */
	if (strict && (!of_get_property(bus, "compatible", NULL))) {
		pr_debug("%s() - skipping %s, no compatible prop\n",
			 __func__, bus->full_name);
		return 0;
	}

	/* Has the device already been registered manually? */
	prep = of_platform_find_prepare_data(bus);
	if (prep && prep->dev) {
		pr_debug("%s() - skipping %s, already registered\n",
			 __func__, bus->full_name);
		return 0;
	}

	auxdata = of_dev_lookup(lookup, bus);
	if (auxdata) {
		bus_id = auxdata->name;
		id = auxdata->id;
		platform_data = auxdata->platform_data;
	}

	if (of_device_is_compatible(bus, "arm,primecell")) {
		of_amba_device_create(bus, bus_id, platform_data, parent);
		return 0;
	}

	dev = of_platform_device_create_pdata(bus, bus_id, platform_data, parent);

	/* override the id if auxdata gives an id */
	if (id != -1)
		dev->id = id;

	if (!dev || !of_match_node(matches, bus))
		return 0;

	for_each_child_of_node(bus, child) {
		pr_debug("   create child: %s\n", child->full_name);
		rc = of_platform_bus_create(child, matches, lookup, &dev->dev, strict);
		if (rc) {
			of_node_put(child);
			break;
		}
	}
	return rc;
}

/**
 * of_platform_bus_probe() - Probe the device-tree for platform buses
 * @root: parent of the first level to probe or NULL for the root of the tree
 * @matches: match table for bus nodes
 * @parent: parent to hook devices from, NULL for toplevel
 *
 * Note that children of the provided root are not instantiated as devices
 * unless the specified root itself matches the bus list and is not NULL.
 */
int of_platform_bus_probe(struct device_node *root,
			  const struct of_device_id *matches,
			  struct device *parent)
{
	struct device_node *child;
	int rc = 0;

	root = root ? of_node_get(root) : of_find_node_by_path("/");
	if (!root)
		return -EINVAL;

	pr_debug("of_platform_bus_probe()\n");
	pr_debug(" starting at: %s\n", root->full_name);

	/* Do a self check of bus type, if there's a match, create children */
	if (of_match_node(matches, root)) {
		rc = of_platform_bus_create(root, matches, NULL, parent, false);
	} else for_each_child_of_node(root, child) {
		if (!of_match_node(matches, child))
			continue;
		rc = of_platform_bus_create(child, matches, NULL, parent, false);
		if (rc)
			break;
	}

	of_node_put(root);
	return rc;
}
EXPORT_SYMBOL(of_platform_bus_probe);

/**
 * of_platform_populate() - Populate platform_devices from device tree data
 * @root: parent of the first level to probe or NULL for the root of the tree
 * @matches: match table, NULL to use the default
 * @parent: parent to hook devices from, NULL for toplevel
 *
 * Similar to of_platform_bus_probe(), this function walks the device tree
 * and creates devices from nodes.  It differs in that it follows the modern
 * convention of requiring all device nodes to have a 'compatible' property,
 * and it is suitable for creating devices which are children of the root
 * node (of_platform_bus_probe will only create children of the root which
 * are selected by the @matches argument).
 *
 * New board support should be using this function instead of
 * of_platform_bus_probe().
 *
 * Returns 0 on success, < 0 on failure.
 */
int of_platform_populate(struct device_node *root,
			const struct of_device_id *matches,
			const struct of_dev_auxdata *lookup,
			struct device *parent)
{
	struct device_node *child;
	int rc = 0;

	root = root ? of_node_get(root) : of_find_node_by_path("/");
	if (!root)
		return -EINVAL;

	for_each_child_of_node(root, child) {
		rc = of_platform_bus_create(child, matches, lookup, parent, true);
		if (rc)
			break;
	}

	of_node_put(root);
	return rc;
}
#endif /* !CONFIG_SPARC */
