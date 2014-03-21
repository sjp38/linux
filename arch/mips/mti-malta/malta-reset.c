/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/pm.h>

#include <asm/reboot.h>
#include <asm/mips-boards/piix4.h>

#define SOFTRES_REG	0x1f000500
#define GORESET		0x42

static void mips_machine_restart(char *command)
{
	unsigned int __iomem *softres_reg =
		ioremap(SOFTRES_REG, sizeof(unsigned int));

	__raw_writel(GORESET, softres_reg);
}

static void mips_machine_halt(void)
{
	struct pci_bus *bus;
	struct pci_dev *dev;
	int spec_devid, res;
	int io_region = PCI_BRIDGE_RESOURCES;
	resource_size_t io;
	u16 sts;

	/* Find the PIIX4 PM device */
	dev = pci_get_subsys(PCI_VENDOR_ID_INTEL,
			     PCI_DEVICE_ID_INTEL_82371AB_3, PCI_ANY_ID,
			     PCI_ANY_ID, NULL);
	if (!dev) {
		printk("Failed to find PIIX4 PM\n");
		goto fail;
	}

	/* Request access to the PIIX4 PM IO registers */
	res = pci_request_region(dev, io_region, "PIIX4 PM IO registers");
	if (res) {
		printk("Failed to request PIIX4 PM IO registers (%d)\n", res);
		goto fail_dev_put;
	}

	/* Find the offset to the PIIX4 PM IO registers */
	io = pci_resource_start(dev, io_region);

	/* Ensure the power button status is clear */
	while (1) {
		sts = inw(io + PIIX4_FUNC3IO_PMSTS);
		if (!(sts & PIIX4_FUNC3IO_PMSTS_PWRBTN_STS))
			break;
		outw(sts, io + PIIX4_FUNC3IO_PMSTS);
	}

	/* Enable entry to suspend */
	outw(PIIX4_FUNC3IO_PMCNTRL_SUS_EN, io + PIIX4_FUNC3IO_PMCNTRL);

	/* If the special cycle occurs too soon this doesn't work... */
	mdelay(10);

	/* Find a reference to the PCI bus */
	bus = pci_find_next_bus(NULL);
	if (!bus) {
		printk("Failed to find PCI bus\n");
		goto fail_release_region;
	}

	/*
	 * The PIIX4 will enter the suspend state only after seeing a special
	 * cycle with the correct magic data on the PCI bus. Generate that
	 * cycle now.
	 */
	spec_devid = PCI_DEVID(0, PCI_DEVFN(0x1f, 0x7));
	pci_bus_write_config_dword(bus, spec_devid, 0, PIIX4_SUSPEND_MAGIC);

	/* Give the system some time to power down */
	mdelay(1000);

	/* If all went well this will never execute */
fail_release_region:
	pci_release_region(dev, io_region);
fail_dev_put:
	pci_dev_put(dev);
fail:
	printk("Failed to power down, resetting\n");
	mips_machine_restart(NULL);
}

static int __init mips_reboot_setup(void)
{
	_machine_restart = mips_machine_restart;
	_machine_halt = mips_machine_halt;
	pm_power_off = mips_machine_halt;

	return 0;
}
arch_initcall(mips_reboot_setup);
