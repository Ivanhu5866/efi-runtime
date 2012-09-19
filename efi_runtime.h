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

typedef unsigned long int	UINTN;
typedef unsigned long long	UINT64;
typedef long long		INT64;
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
} __attribute__ ((packed)) EFI_GUID;

typedef struct {
	UINT16	Year;		/* 1900 – 9999 */
	UINT8	Month;		/* 1 – 12 */
	UINT8	Day;		/* 1 – 31 */
	UINT8	Hour;		/* 0 – 23 */
	UINT8	Minute;		/* 0 – 59 */
	UINT8	Second;		/* 0 – 59 */
	UINT8	Pad1;
	UINT32	Nanosecond;	/* 0 – 999,999,999 */
	INT16	TimeZone;	/* -1440 to 1440 or 2047 */
	UINT8	Daylight;
	UINT8	Pad2;
} __attribute__ ((packed)) EFI_TIME;

typedef struct {
	UINT32	Resolution;
	UINT32	Accuracy;
	BOOLEAN	SetsToZero;
} __attribute__ ((packed)) EFI_TIME_CAPABILITIES;

struct efi_getvariable {
	CHAR16		*VariableName;
	EFI_GUID	*VendorGuid;
	UINT32		*Attributes;
	UINTN		*DataSize;
	VOID		*Data;
} __attribute__ ((packed));

struct efi_setvariable {
	CHAR16		*VariableName;
	EFI_GUID	*VendorGuid;
	UINT32		Attributes;
	UINTN		DataSize;
	VOID		*Data;
} __attribute__ ((packed));

struct efi_getnextvariablename {
	UINTN		*VariableNameSize;
	CHAR16		*VariableName;
	EFI_GUID	*VendorGuid;
} __attribute__ ((packed));

struct efi_gettime {
	EFI_TIME		*Time;
	EFI_TIME_CAPABILITIES	*Capabilities;
} __attribute__ ((packed));

struct efi_settime {
	EFI_TIME		*Time;
} __attribute__ ((packed));

struct efi_getwakeuptime {
	BOOLEAN		*Enabled;
	BOOLEAN		*Pending;
	EFI_TIME	*Time;
} __attribute__ ((packed));

struct efi_setwakeuptime {
	BOOLEAN		Enabled;
	EFI_TIME	*Time;
} __attribute__ ((packed));

/* ioctl calls that are permitted to the /dev/efi_runtime interface. */
#define EFI_RUNTIME_GET_VARIABLE \
	_IOWR('p', 0x01, struct efi_getvariable)
#define EFI_RUNTIME_SET_VARIABLE \
	_IOW('p', 0x02, struct efi_setvariable)

#define EFI_RUNTIME_GET_TIME \
	_IOR('p', 0x03, struct efi_gettime)
#define EFI_RUNTIME_SET_TIME \
	_IOW('p', 0x04, struct efi_settime)

#define EFI_RUNTIME_GET_WAKETIME \
	_IOR('p', 0x05, struct efi_getwakeuptime)
#define EFI_RUNTIME_SET_WAKETIME \
	_IOW('p', 0x06, struct efi_setwakeuptime)

#define EFI_RUNTIME_GET_NEXTVARIABLENAME \
	_IOWR('p', 0x07, struct efi_getnextvariablename)

#endif /* _EFI_RUNTIME_H_ */
