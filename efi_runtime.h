/*
 * EFI Runtime driver
 *
 * Copyright(C) 2012 Canonical Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _EFI_RUNTIME_H_
#define _EFI_RUNTIME_H_

#include <linux/types.h>

struct efi_getvariable {
	unsigned short	*variable_name;
	unsigned char	VendorGuid[16];
	unsigned int	*Attributes;
	unsigned long	*DataSize;
	void		*Data;
};

struct efi_setvariable {
	unsigned short	*variable_name;
	unsigned char	VendorGuid[16];
	unsigned int	Attributes;
	unsigned long	DataSize;
	void		*Data;
};

/*
 * ioctl calls that are permitted to the /dev/efi_runtime interface.
 */
#define EFI_RUNTIME_GET_VARIABLE _IOWR('p', 0x01, struct efi_getvariable)
#define EFI_RUNTIME_SET_VARIABLE _IOW('p', 0x02, struct efi_setvariable)

#endif /* _EFI_RUNTIME_H_ */
