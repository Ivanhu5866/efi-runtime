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

typedef unsigned long		UINTN;
typedef unsigned long		UINT64;
typedef long			INT64;
typedef unsigned int		UINT32;
typedef int			INT32;
typedef unsigned short		UINT16;
typedef unsigned short		CHAR16;
typedef short			INT16;
typedef unsigned char		BOOLEAN;
typedef unsigned char		UINT8;
typedef char			CHAR8;
typedef char			INT8;
typedef void			VOID;

typedef struct {
  UINT32  Data1;
  UINT16  Data2;
  UINT16  Data3;
  UINT8   Data4[8];
} EFI_GUID;

struct efi_getvariable {
	CHAR16		*variable_name;
	EFI_GUID	*VendorGuid;
	UINT32		*Attributes;
	UINTN		*DataSize;
	VOID		*Data;
};

struct efi_setvariable {
	CHAR16		*variable_name;
	EFI_GUID	*VendorGuid;
	UINT32		Attributes;
	UINTN		DataSize;
	VOID		*Data;
};

/* ioctl calls that are permitted to the /dev/efi_runtime interface. */

#define EFI_RUNTIME_GET_VARIABLE _IOWR('p', 0x01, struct efi_getvariable)
#define EFI_RUNTIME_SET_VARIABLE _IOW('p', 0x02, struct efi_setvariable)

#endif /* _EFI_RUNTIME_H_ */
