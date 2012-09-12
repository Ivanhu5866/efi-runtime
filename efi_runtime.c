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

#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/efi.h>

#include <linux/uaccess.h>

#include "efi_runtime.h"

#define EFI_GUID_CONV(a, b, c, d) \
((efi_guid_t) \
{{(a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, ((a) >> 24) & 0xff, \
(b) & 0xff, ((b) >> 8) & 0xff, \
(c) & 0xff, ((c) >> 8) & 0xff, \
(d[0]), (d[1]), (d[2]), (d[3]), (d[4]), (d[5]), (d[6]), (d[7]) } })

#define EFI_FWTSEFI_VERSION	"0.1"

MODULE_AUTHOR("Ivan Hu");
MODULE_DESCRIPTION("EFI Runtime Driver");
MODULE_LICENSE("GPL");

static void convert_from_efi_time(efi_time_t *eft, EFI_TIME *time)
{
	memset(time, 0, sizeof(EFI_TIME));
	time->Year = eft->year;
	time->Month = eft->month;
	time->Day  = eft->day;
	time->Hour = eft->hour;
	time->Minute = eft->minute;
	time->Second  = eft->second;
	time->Pad1 = eft->pad1;
	time->Nanosecond = eft->nanosecond;
	time->TimeZone = eft->timezone;
	time->Daylight = eft->daylight;
	time->Pad2 = eft->pad2;
}

static void convert_to_efi_time(efi_time_t *eft, EFI_TIME *time)
{
	memset(eft, 0, sizeof(eft));
	eft->year = time->Year;
	eft->month = time->Month;
	eft->day = time->Day;
	eft->hour = time->Hour;
	eft->minute = time->Minute;
	eft->second = time->Second;
	eft->pad1 = time->Pad1;
	eft->nanosecond = time->Nanosecond;
	eft->timezone = time->TimeZone;
	eft->daylight = time->Daylight;
	eft->pad2 = time->Pad2;
}

static long efi_runtime_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	efi_status_t status;
	struct efi_getvariable __user *pgetvariable;
	struct efi_setvariable __user *psetvariable;

	efi_char16_t name[1024/sizeof(efi_char16_t)];
	efi_guid_t vendor;
	EFI_GUID vendor_guid;
	unsigned long datasize;
	UINT32 attr;

	efi_time_t eft;
	efi_time_cap_t cap;
	struct efi_gettime __user *pgettime;
	struct efi_settime __user *psettime;

	unsigned char enabled, pending;
	EFI_TIME efi_time;
	struct efi_getwakeuptime __user *pgetwakeuptime;
	struct efi_setwakeuptime __user *psetwakeuptime;
	int i;

	switch (cmd) {
	case EFI_RUNTIME_GET_VARIABLE:
		pgetvariable = (struct efi_getvariable __user *)arg;
		i = 0;
		while (pgetvariable->VariableName[i] != '\0') {
			name[i] = pgetvariable->VariableName[i];
			i++;
		};
		name[i] = '\0';
		if (get_user(datasize, pgetvariable->DataSize) ||
			copy_from_user(&vendor_guid, pgetvariable->VendorGuid,
							sizeof(EFI_GUID)))
			return -EFAULT;

		vendor = EFI_GUID_CONV(vendor_guid.Data1, vendor_guid.Data2,
					vendor_guid.Data3, vendor_guid.Data4);
		status = efi.get_variable(name, &vendor, &attr, &datasize,
							pgetvariable->Data);

		if (status == EFI_SUCCESS) {
			if (put_user(attr, pgetvariable->Attributes) ||
				put_user(datasize, pgetvariable->DataSize))
				return -EFAULT;
			return 0;
		} else {
			printk(KERN_ERR "efi_runtime: can't get variable\n");
			return -EINVAL;
		}

	case EFI_RUNTIME_SET_VARIABLE:
		psetvariable = (struct efi_setvariable __user *)arg;
		i = 0;
		while (psetvariable->VariableName[i] != '\0') {
			name[i] = psetvariable->VariableName[i];
			i++;
		};
		name[i] = '\0';
		if (get_user(datasize, &psetvariable->DataSize) ||
			get_user(attr, &psetvariable->Attributes) ||
			copy_from_user(&vendor_guid, psetvariable->VendorGuid,
							sizeof(EFI_GUID)))
			return -EFAULT;

		vendor = EFI_GUID_CONV(vendor_guid.Data1, vendor_guid.Data2,
					vendor_guid.Data3, vendor_guid.Data4);
		status = efi.set_variable(name, &vendor, attr, datasize,
							psetvariable->Data);
		return status == EFI_SUCCESS ? 0 : -EINVAL;

	case EFI_RUNTIME_GET_TIME:
		status = efi.get_time(&eft, &cap);
		if (status != EFI_SUCCESS) {
			printk(KERN_ERR "efitime: can't read time\n");
			return -EINVAL;
		}

		pgettime = (struct efi_gettime __user *)arg;
		if (put_user(cap.resolution,
					&pgettime->Capabilities->Resolution) ||
					put_user(cap.accuracy,
					&pgettime->Capabilities->Accuracy) ||
					put_user(cap.sets_to_zero,
					&pgettime->Capabilities->SetsToZero))
			return -EFAULT;
		return copy_to_user(pgettime->Time, &eft,
				sizeof(EFI_TIME)) ? -EFAULT : 0;

	case EFI_RUNTIME_SET_TIME:

		psettime = (struct efi_settime __user *)arg;
		if (copy_from_user(&efi_time, psettime->Time,
						sizeof(EFI_TIME)))
			return -EFAULT;
		convert_to_efi_time(&eft, &efi_time);
		status = efi.set_time(&eft);
		return status == EFI_SUCCESS ? 0 : -EINVAL;

	case EFI_RUNTIME_GET_WAKETIME:

		status = efi.get_wakeup_time((efi_bool_t *)&enabled,
						(efi_bool_t *)&pending, &eft);

		if (status != EFI_SUCCESS)
			return -EINVAL;

		pgetwakeuptime = (struct efi_getwakeuptime __user *)arg;

		if (put_user(enabled, pgetwakeuptime->Enabled) ||
				put_user(pending, pgetwakeuptime->Pending))
			return -EFAULT;

		convert_from_efi_time(&eft, &efi_time);

		return copy_to_user(pgetwakeuptime->Time, &efi_time,
				sizeof(EFI_TIME)) ? -EFAULT : 0;

	case EFI_RUNTIME_SET_WAKETIME:

		psetwakeuptime = (struct efi_setwakeuptime __user *)arg;

		if (get_user(enabled, &psetwakeuptime->Enabled) ||
					copy_from_user(&efi_time,
					psetwakeuptime->Time,
					sizeof(EFI_TIME)))
			return -EFAULT;

		convert_to_efi_time(&eft, &efi_time);

		status = efi.set_wakeup_time(enabled, &eft);

		return status == EFI_SUCCESS ? 0 : -EINVAL;
	}

	return -ENOTTY;
}

static int efi_runtime_open(struct inode *inode, struct file *file)
{
	/*
	 * nothing special to do here
	 * We do accept multiple open files at the same time as we
	 * synchronize on the per call operation.
	 */
	return 0;
}

static int efi_runtime_close(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 *	The various file operations we support.
 */
static const struct file_operations efi_runtime_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= efi_runtime_ioctl,
	.open		= efi_runtime_open,
	.release	= efi_runtime_close,
	.llseek		= no_llseek,
};

static struct miscdevice efi_runtime_dev = {
	MISC_DYNAMIC_MINOR,
	"efi_runtime",
	&efi_runtime_fops
};

static int __init efi_runtime_init(void)
{
	int ret;

	printk(KERN_INFO "EFI_RUNTIME Driver v%s\n", EFI_FWTSEFI_VERSION);

	ret = misc_register(&efi_runtime_dev);
	if (ret) {
		printk(KERN_ERR "efi_runtime: can't misc_register on minor=%d\n",
				MISC_DYNAMIC_MINOR);
		return ret;
	}

	return 0;
}

static void __exit efi_runtime_exit(void)
{
	printk(KERN_INFO "EFI_RUNTIME Driver Exit.\n");
	misc_deregister(&efi_runtime_dev);
}

module_init(efi_runtime_init);
module_exit(efi_runtime_exit);

