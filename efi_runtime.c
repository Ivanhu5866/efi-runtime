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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <linux/version.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/efi.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "efi_runtime.h"

#define EFI_FWTS_EFI_VERSION	"0.1"

MODULE_AUTHOR("Ivan Hu");
MODULE_DESCRIPTION("EFI Runtime Driver");
MODULE_LICENSE("GPL");

/* commit 83e681897 turned efi_enabled into a function, so abstract it */
#ifdef EFI_RUNTIME_SERVICES
#define EFI_RUNTIME_ENABLED	efi_enabled(EFI_RUNTIME_SERVICES)
#else
#define EFI_RUNTIME_ENABLED	efi_enabled
#endif

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
	memset(eft, 0, sizeof(*eft));
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

static void convert_from_guid(efi_guid_t *vendor, EFI_GUID *vendor_guid)
{
	int i;
	for (i = 0; i < 16; i++) {
		if (i < 4)
			vendor->b[i] = (vendor_guid->Data1 >> (8*i)) & 0xff;
		else if (i < 6)
			vendor->b[i] = (vendor_guid->Data2 >> (8*(i-4))) & 0xff;
		else if (i < 8)
			vendor->b[i] = (vendor_guid->Data3 >> (8*(i-6))) & 0xff;
		else
			vendor->b[i] = (vendor_guid->Data4[i-8]);
	}
}

static void convert_to_guid(efi_guid_t *vendor, EFI_GUID *vendor_guid)
{
	int i;
	vendor_guid->Data1 = vendor->b[0] + (vendor->b[1] << 8) +
				(vendor->b[2] << 16) + (vendor->b[3] << 24);
	vendor_guid->Data2 = vendor->b[4] + (vendor->b[5] << 8);
	vendor_guid->Data3 = vendor->b[6] + (vendor->b[7] << 8);
	for (i = 0; i < 8; i++)
		vendor_guid->Data4[i] = vendor->b[i+8];
}

/*
 * Count the bytes in 'str', including the terminating NULL.
 *
 * Note this function returns the number of *bytes*, not the number of
 * ucs2 characters.
 */
static inline size_t __ucs2_strsize(uint16_t  __user *str)
{
	uint16_t *s = str, c;
	size_t len;

	if (!str)
		return 0;

	/* Include terminating NULL */
	len = sizeof(uint16_t);

	if (get_user(c, s++)) {
		WARN(1, "fwts: Can't read userspace memory for size");
		return 0;
	}

	while (c != 0) {
		if (get_user(c, s++)) {
			WARN(1, "Can't read userspace memory for size");
			return 0;
		}
		len += sizeof(uint16_t);
	}
	return len;
}

/*
 * Free a buffer allocated by copy_ucs2_from_user_len()
 */
static inline void ucs2_kfree(uint16_t *buf)
{
	if (buf)
		kfree(buf - 1);
}

/*
 * Allocate a buffer and copy a ucs2 string from user space into it.
 *
 * We take an explicit number of bytes to use for the allocation and
 * copy, and therefore do not make any assumptions about 'src' (such as
 * it pointing to a valid string).
 *
 * If a non-zero value is returned, the caller MUST NOT access 'dst'.
 *
 * It is the caller's responsibility to free 'dst' using ucs2_kfree()
 *
 * We cater for zero sized len by always allocating a buffer that is 1
 * uint16_t larger than the requested size and passing back the buffer
 * offset by 1 uint16_t.  That way, the returned buffer size is the
 * size that is requested and the buffer ptr is a valid pointer (and not
 * NULL or ZERO_SIZE_PTR).  This allows EFI services to be passed a valid
 * allocated buffer of zero length size and to see if the services handle
 * this as an EFI_BUFFER_TOO_SMALL error.
 * 
 */
static inline int
copy_ucs2_from_user_len(uint16_t **dst, uint16_t __user *src, size_t len)
{
	uint16_t *buf;

	if (!src) {
		*dst = NULL;
		return 0;
	}

	if (!access_ok(VERIFY_READ, src, 1))
		return -EFAULT;

	buf = kmalloc(len + sizeof(uint16_t), GFP_KERNEL);
	if (!buf) {
		*dst = 0;
		return -ENOMEM;
	}
	*dst = buf + 1;

	if (copy_from_user(*dst, src, len)) {
		kfree(buf);
		return -EFAULT;
	}

	return 0;
}

/*
 * Calculate the required buffer allocation size and copy a ucs2 string
 * from user space into it.
 *
 * This function differs from copy_ucs2_from_user_len() because it
 * calculates the size of the buffer to allocate by taking the length of
 * the string 'src'.
 *
 * If a non-zero value is returned, the caller MUST NOT access 'dst'.
 *
 * It is the caller's responsibility to free 'dst'.
 */
static inline int copy_ucs2_from_user(uint16_t **dst, uint16_t __user *src)
{
	size_t len;

	if (!access_ok(VERIFY_READ, src, 1))
		return -EFAULT;

	len = __ucs2_strsize(src);
	return copy_ucs2_from_user_len(dst, src, len);
}

/*
 * Copy a ucs2 string to a user buffer.
 *
 * This function is a simple wrapper around copy_to_user() that does
 * nothing if 'src' is NULL, which is useful for reducing the amount of
 * NULL checking the caller has to do.
 *
 * 'len' specifies the number of bytes to copy.
 */
static inline int
copy_ucs2_to_user_len(uint16_t __user *dst, uint16_t *src, size_t len)
{
	if (!src)
		return 0;

	if (!access_ok(VERIFY_WRITE, dst, 1))
		return -EFAULT;

	return copy_to_user(dst, src, len);
}

static long efi_runtime_get_variable(unsigned long arg)
{
	struct efi_getvariable __user *pgetvariable;
	struct efi_getvariable pgetvariable_local;
	unsigned long datasize, prev_datasize, *pdatasize;
	efi_guid_t vendor, *pvendor = NULL;
	efi_status_t status;
	uint16_t *name = NULL;
	uint32_t attr, *pattr;
	void *data = NULL;
	int rv = 0;

	pgetvariable = (struct efi_getvariable __user *)arg;

	if (copy_from_user(&pgetvariable_local, pgetvariable,
			   sizeof(pgetvariable_local)))
		return -EFAULT;
	if (pgetvariable_local.DataSize &&
	    get_user(datasize, pgetvariable_local.DataSize))
		return -EFAULT;
	if (pgetvariable_local.VendorGuid) {
		EFI_GUID vendor_guid;

		if (copy_from_user(&vendor_guid, pgetvariable_local.VendorGuid,
			   sizeof(vendor_guid)))
			return -EFAULT;
		convert_from_guid(&vendor, &vendor_guid);
		pvendor = &vendor;
	}

	if (pgetvariable_local.VariableName) {
		rv = copy_ucs2_from_user(&name, pgetvariable_local.VariableName);
		if (rv)
			return rv;
	}

	pattr = pgetvariable_local.Attributes ? &attr : NULL;
	pdatasize = pgetvariable_local.DataSize ? &datasize : NULL;

	if (pgetvariable_local.DataSize && pgetvariable_local.Data) {
		data = kmalloc(datasize, GFP_KERNEL);
		if (!data) {
			ucs2_kfree(name);
			return -ENOMEM;
		}
	}

	prev_datasize = datasize;
	status = efi.get_variable(name, pvendor, pattr, pdatasize, data);
	ucs2_kfree(name);

	if (data) {
		if (status == EFI_SUCCESS && prev_datasize >= datasize)
			rv = copy_to_user(pgetvariable_local.Data, data, datasize);
		kfree(data);
	}

	if (rv)
		return rv;

	if (put_user(status, pgetvariable_local.status))
		return -EFAULT;
	if (status == EFI_SUCCESS && prev_datasize >= datasize) {
		if (pattr && put_user(attr, pgetvariable_local.Attributes))
			return -EFAULT;
		if (pdatasize && put_user(datasize, pgetvariable_local.DataSize))
			return -EFAULT;
		return 0;
	} else {
		return -EINVAL;
	}

	return 0;
}

static long efi_runtime_set_variable(unsigned long arg)
{
	struct efi_setvariable __user *psetvariable;
	struct efi_setvariable psetvariable_local;
	EFI_GUID vendor_guid;
	efi_guid_t vendor;
	efi_status_t status;
	uint16_t *name;
	void *data;
	int rv;

	psetvariable = (struct efi_setvariable __user *)arg;

	if (copy_from_user(&psetvariable_local, psetvariable,
			   sizeof(psetvariable_local)))
		return -EFAULT;
	if (copy_from_user(&vendor_guid, psetvariable_local.VendorGuid,
			   sizeof(vendor_guid)))
		return -EFAULT;

	convert_from_guid(&vendor, &vendor_guid);

	rv = copy_ucs2_from_user(&name, psetvariable_local.VariableName);
	if (rv)
		return rv;

	data = kmalloc(psetvariable_local.DataSize, GFP_KERNEL);
	if (!data) {
		ucs2_kfree(name);
		return -ENOMEM;
	}
	if (copy_from_user(data, psetvariable_local.Data,
			   psetvariable_local.DataSize)) {
		ucs2_kfree(data);
		kfree(name);
		return -EFAULT;
	}

	status = efi.set_variable(name, &vendor, psetvariable_local.Attributes,
				  psetvariable_local.DataSize, data);

	kfree(data);
	ucs2_kfree(name);

	if (put_user(status, psetvariable_local.status))
		return -EFAULT;
	return status == EFI_SUCCESS ? 0 : -EINVAL;
}

static long efi_runtime_get_time(unsigned long arg)
{
	struct efi_gettime __user *pgettime;
	struct efi_gettime  pgettime_local;
	efi_status_t status;
	efi_time_cap_t cap;
	efi_time_t eft;

	pgettime = (struct efi_gettime __user *)arg;
	if (copy_from_user(&pgettime_local, pgettime, sizeof(pgettime_local)))
		return -EFAULT;

	status = efi.get_time(pgettime_local.Time ? &eft : NULL,
			      pgettime_local.Capabilities ? &cap : NULL);

	if (put_user(status, pgettime_local.status))
		return -EFAULT;
	if (status != EFI_SUCCESS) {
		printk(KERN_ERR "efitime: can't read time\n");
		return -EINVAL;
	}
	if (pgettime_local.Capabilities) {
		EFI_TIME_CAPABILITIES __user *cap_local;

		cap_local = (EFI_TIME_CAPABILITIES *)pgettime_local.Capabilities;
		if (put_user(cap.resolution,
				&(cap_local->Resolution)) ||
				put_user(cap.accuracy, &(cap_local->Accuracy)) ||
				put_user(cap.sets_to_zero,&(cap_local->SetsToZero)))
			return -EFAULT;
	}
	if (pgettime_local.Time)
		return copy_to_user(pgettime_local.Time, &eft,
			sizeof(EFI_TIME)) ? -EFAULT : 0;
	return 0;
}

static long efi_runtime_set_time(unsigned long arg)
{
	struct efi_settime __user *psettime;
	struct efi_settime psettime_local;
	efi_status_t status;
	EFI_TIME efi_time;
	efi_time_t eft;

	psettime = (struct efi_settime __user *)arg;
	if (copy_from_user(&psettime_local, psettime, sizeof(psettime_local)))
		return -EFAULT;
	if (copy_from_user(&efi_time, psettime_local.Time,
					sizeof(EFI_TIME)))
		return -EFAULT;
	convert_to_efi_time(&eft, &efi_time);
	status = efi.set_time(&eft);

	if (put_user(status, psettime_local.status))
		return -EFAULT;

	return status == EFI_SUCCESS ? 0 : -EINVAL;
}

static long efi_runtime_get_waketime(unsigned long arg)
{
	struct efi_getwakeuptime __user *pgetwakeuptime;
	struct efi_getwakeuptime pgetwakeuptime_local;
	unsigned char enabled, pending;
	efi_status_t status;
	EFI_TIME efi_time;
	efi_time_t eft;

	pgetwakeuptime = (struct efi_getwakeuptime __user *)arg;
	if (copy_from_user(&pgetwakeuptime_local, pgetwakeuptime, sizeof(pgetwakeuptime_local)))
		return -EFAULT;

	status = efi.get_wakeup_time(
		pgetwakeuptime_local.Enabled ? (efi_bool_t *)&enabled : NULL,
		pgetwakeuptime_local.Pending ? (efi_bool_t *)&pending : NULL,
		pgetwakeuptime_local.Time ? &eft : NULL);

	if (put_user(status, pgetwakeuptime_local.status))
		return -EFAULT;
	if (status != EFI_SUCCESS)
		return -EINVAL;
	if (pgetwakeuptime_local.Enabled && put_user(enabled, pgetwakeuptime_local.Enabled))
		return -EFAULT;
	convert_from_efi_time(&eft, &efi_time);

	if (pgetwakeuptime_local.Time)
		return copy_to_user(pgetwakeuptime_local.Time, &efi_time,
			sizeof(EFI_TIME)) ? -EFAULT : 0;
	return 0;
}

static long efi_runtime_set_waketime(unsigned long arg)
{
	struct efi_setwakeuptime __user *psetwakeuptime;
	struct efi_setwakeuptime psetwakeuptime_local;
	unsigned char enabled;
	efi_status_t status;
	EFI_TIME efi_time;
	efi_time_t eft;

	psetwakeuptime = (struct efi_setwakeuptime __user *)arg;

	if (copy_from_user(&psetwakeuptime_local, psetwakeuptime, sizeof(psetwakeuptime_local)))
		return -EFAULT;

	enabled = psetwakeuptime_local.Enabled;
	if (psetwakeuptime_local.Time) {
		if (copy_from_user(&efi_time, psetwakeuptime_local.Time, sizeof(EFI_TIME)))
			return -EFAULT;

		convert_to_efi_time(&eft, &efi_time);
		status = efi.set_wakeup_time(enabled, &eft);
	} else {
		status = efi.set_wakeup_time(enabled, NULL);
	}

	if (put_user(status, psetwakeuptime_local.status))
		return -EFAULT;

	return status == EFI_SUCCESS ? 0 : -EINVAL;
}

static long efi_runtime_get_nextvariablename(unsigned long arg)
{
	struct efi_getnextvariablename __user *pgetnextvariablename;
	struct efi_getnextvariablename pgetnextvariablename_local;
	unsigned long name_size, prev_name_size;
	efi_status_t status;
	efi_guid_t vendor;
	EFI_GUID vendor_guid;
	uint16_t *name;
	int rv;

	pgetnextvariablename = (struct efi_getnextvariablename
							__user *)arg;

	if (copy_from_user(&pgetnextvariablename_local, pgetnextvariablename,
			   sizeof(pgetnextvariablename_local)))
		return -EFAULT;

	if (get_user(name_size, pgetnextvariablename_local.VariableNameSize) ||
	    copy_from_user(&vendor_guid, pgetnextvariablename_local.VendorGuid,
			   sizeof(vendor_guid)))
		return -EFAULT;

	convert_from_guid(&vendor, &vendor_guid);

	rv = copy_ucs2_from_user_len(&name,
				     pgetnextvariablename_local.VariableName,
				     name_size);
	if (rv)
		return rv;

	prev_name_size = name_size;
	status = efi.get_next_variable(&name_size, name, &vendor);

	if (status == EFI_SUCCESS && prev_name_size >= name_size)
		rv = copy_ucs2_to_user_len(pgetnextvariablename_local.VariableName,
					   name, name_size);
	ucs2_kfree(name);

	if (rv)
		return -EFAULT;

	if (put_user(status, pgetnextvariablename_local.status))
		return -EFAULT;
	convert_to_guid(&vendor, &vendor_guid);

	if (put_user(name_size, pgetnextvariablename_local.VariableNameSize))
		return -EFAULT;

	if (copy_to_user(pgetnextvariablename_local.VendorGuid,
			 &vendor_guid, sizeof(EFI_GUID)))
		return -EFAULT;
	if (status != EFI_SUCCESS || name_size > prev_name_size)
		return -EINVAL;
	return 0;
}

static long efi_runtime_get_nexthighmonocount(unsigned long arg)
{
	struct efi_getnexthighmonotoniccount __user *pgetnexthighmonotoniccount;
	struct efi_getnexthighmonotoniccount pgetnexthighmonotoniccount_local;
	efi_status_t status;
	uint32_t count;

	pgetnexthighmonotoniccount = (struct
			efi_getnexthighmonotoniccount __user *)arg;

	if (copy_from_user(&pgetnexthighmonotoniccount_local,
			   pgetnexthighmonotoniccount,
			   sizeof(pgetnexthighmonotoniccount_local)))
		return -EFAULT;

	status = efi.get_next_high_mono_count(
		pgetnexthighmonotoniccount_local.HighCount ? &count : NULL);

	if (put_user(status, pgetnexthighmonotoniccount_local.status))
		return -EFAULT;

	if (pgetnexthighmonotoniccount_local.HighCount &&
	    put_user(count, pgetnexthighmonotoniccount_local.HighCount))
		return -EFAULT;

	if (status != EFI_SUCCESS)
		return -EINVAL;

	return 0;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0)
static long efi_runtime_query_variableinfo(unsigned long arg)
{
	struct efi_queryvariableinfo __user *pqueryvariableinfo;
	struct efi_queryvariableinfo pqueryvariableinfo_local;
	efi_status_t status;
	uint64_t max_storage, remaining, max_size;

	pqueryvariableinfo = (struct efi_queryvariableinfo __user *)arg;

	if (copy_from_user(&pqueryvariableinfo_local, pqueryvariableinfo,
			   sizeof(pqueryvariableinfo_local)))
		return -EFAULT;

	status = efi.query_variable_info(pqueryvariableinfo_local.Attributes,
					 &max_storage, &remaining, &max_size);

	if (put_user(max_storage,
		     pqueryvariableinfo_local.MaximumVariableStorageSize))
		return -EFAULT;

	if (put_user(remaining,
		     pqueryvariableinfo_local.RemainingVariableStorageSize))
		return -EFAULT;

	if (put_user(max_size, pqueryvariableinfo_local.MaximumVariableSize))
		return -EFAULT;

	if (put_user(status, pqueryvariableinfo_local.status))
		return -EFAULT;
	if (status != EFI_SUCCESS)
		return -EINVAL;

	return 0;
}

static long efi_runtime_query_capsulecaps(unsigned long arg)
{
	struct efi_querycapsulecapabilities __user *u_caps;
	struct efi_querycapsulecapabilities caps;
	EFI_CAPSULE_HEADER *capsules;
	efi_status_t status;
	uint64_t max_size;
	int i, reset_type;

	u_caps = (struct efi_querycapsulecapabilities __user *)arg;

	if (copy_from_user(&caps, u_caps, sizeof(caps)))
		return -EFAULT;

	capsules = kcalloc(caps.CapsuleCount + 1,
			   sizeof(EFI_CAPSULE_HEADER), GFP_KERNEL);
	if (!capsules)
		return -ENOMEM;

	for (i = 0; i < caps.CapsuleCount; i++) {
		EFI_CAPSULE_HEADER *c;
		/*
		 * We cannot dereference caps.CapsuleHeaderArray directly to
		 * obtain the address of the capsule as it resides in the
		 * user space
		 */
		if (get_user(c, caps.CapsuleHeaderArray + i))
			return -EFAULT;
		if (copy_from_user(&capsules[i], c, sizeof(EFI_CAPSULE_HEADER)))
			return -EFAULT;
	}

	caps.CapsuleHeaderArray = &capsules;

	status = efi.query_capsule_caps((efi_capsule_header_t **)
					caps.CapsuleHeaderArray,
					caps.CapsuleCount,
					&max_size, &reset_type);

	if (put_user(status, caps.status))
		return -EFAULT;

	if (put_user(max_size, caps.MaximumCapsuleSize))
		return -EFAULT;

	if (put_user(reset_type, caps.ResetType))
		return -EFAULT;

	if (status != EFI_SUCCESS)
		return -EINVAL;

	return 0;
}
#endif

static long efi_runtime_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	switch (cmd) {
	case EFI_RUNTIME_GET_VARIABLE:
		return efi_runtime_get_variable(arg);

	case EFI_RUNTIME_SET_VARIABLE:
		return efi_runtime_set_variable(arg);

	case EFI_RUNTIME_GET_TIME:
		return efi_runtime_get_time(arg);

	case EFI_RUNTIME_SET_TIME:
		return efi_runtime_set_time(arg);

	case EFI_RUNTIME_GET_WAKETIME:
		return efi_runtime_get_waketime(arg);

	case EFI_RUNTIME_SET_WAKETIME:
		return efi_runtime_set_waketime(arg);

	case EFI_RUNTIME_GET_NEXTVARIABLENAME:
		return efi_runtime_get_nextvariablename(arg);

	case EFI_RUNTIME_GET_NEXTHIGHMONOTONICCOUNT:
		return efi_runtime_get_nexthighmonocount(arg);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0)
	case EFI_RUNTIME_QUERY_VARIABLEINFO:
		return efi_runtime_query_variableinfo(arg);

	case EFI_RUNTIME_QUERY_CAPSULECAPABILITIES:
		return efi_runtime_query_capsulecaps(arg);
#endif
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

	printk(KERN_INFO "EFI_RUNTIME Driver v%s\n", EFI_FWTS_EFI_VERSION);

	if (!EFI_RUNTIME_ENABLED) {
		printk(KERN_INFO "EFI runtime services not enabled.\n");
 		return -ENODEV;
	}

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

