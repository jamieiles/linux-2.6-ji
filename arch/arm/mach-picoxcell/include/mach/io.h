/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define PHYS_TO_IO(x)		(((x) & 0x00ffffff) | 0xfe000000)

#ifdef __ASSEMBLY__
# define IO_ADDRESS(x)		PHYS_TO_IO((x))
#else /* __ASSEMBLY__ */
# define IO_ADDRESS(x)		(void __iomem __force *)(PHYS_TO_IO((x)))
# define IO_SPACE_LIMIT		0xffffffff
# define __io(a)		__typesafe_io(a)
# define __mem_pci(a)		(a)
# define __arch_ioremap		picoxcell_ioremap
# define __arch_iounmap		picoxcell_iounmap

void __iomem *picoxcell_ioremap(unsigned long phys, size_t size,
				unsigned int type);
void picoxcell_iounmap(volatile void __iomem *addr);
#endif /* __ASSEMBLY__ */

#endif /* __ASM_ARM_ARCH_IO_H */
