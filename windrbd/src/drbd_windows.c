﻿/*
	Copyright(C) 2017-2018, Johannes Thoma <johannes@johannesthoma.com>
	Copyright(C) 2017-2018, LINBIT HA-Solutions GmbH  <office@linbit.com>
	Copyright(C) 2007-2016, ManTechnology Co., LTD.
	Copyright(C) 2007-2016, wdrbd@mantech.co.kr

	Windows DRBD is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2, or (at your option)
	any later version.

	Windows DRBD is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Windows DRBD; see the file COPYING. If not, write to
	the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* Uncomment this if you want more debug output (disable for releases) */
/* #define DEBUG 1 */

#ifdef RELEASE
#ifdef DEBUG
#undef DEBUG
#endif
#endif

#include <initguid.h>

#include "drbd_windows.h"
#include "windrbd_device.h"
#include "windrbd_threads.h"
#include <wdmsec.h>
#include <ntdddisk.h>
#include <wdm.h>
#include <wdmguid.h>
// #include <ntddstor.h>
#include <IoEvent.h>

#include <mountmgr.h>
#include "drbd_int.h"

	/* TODO: split this up into several files. Already done for
	 * threads, but there's much more ...
	 *
	 * Also split up the drbd_windows.h header file.
	 */

#undef _NTDDK_
/* Can't include that without getting redefinition errors.
 *   10.0.14393.0\km\ntddk.h(69): error C2371: 'PEPROCESS': redefinition; different basic types
 *   10.0.14393.0\km\wdm.h(84): note: see declaration of 'PEPROCESS'
#include <ntifs.h>
 *   */
NTSTATUS
NTAPI
ZwCreateEvent (
	_Out_ PHANDLE EventHandle,
	_In_ ACCESS_MASK DesiredAccess,
	_In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
	_In_ EVENT_TYPE EventType,
	_In_ BOOLEAN InitialState
	);
NTSTATUS
NTAPI
ZwWaitForSingleObject(
	_In_ HANDLE Handle,
	_In_ BOOLEAN Alertable,
	_In_opt_ PLARGE_INTEGER Timeout
	);

#define FSCTL_DISMOUNT_VOLUME           CTL_CODE(FILE_DEVICE_FILE_SYSTEM,  8, METHOD_BUFFERED, FILE_ANY_ACCESS)

NTSTATUS ZwFsControlFile(
  HANDLE           FileHandle,
  HANDLE           Event,
  PIO_APC_ROUTINE  ApcRoutine,
  PVOID            ApcContext,
  PIO_STATUS_BLOCK IoStatusBlock,
  ULONG            FsControlCode,
  PVOID            InputBuffer,
  ULONG            InputBufferLength,
  PVOID            OutputBuffer,
  ULONG            OutputBufferLength
);


ULONG RtlRandomEx(
  _Inout_ PULONG Seed
);


#include <ntddk.h>
#include <stdint.h>
#include <stdarg.h>
#include <intrin.h>
#include "drbd_wingenl.h"
#include "linux/idr.h"
#include "drbd_wrappers.h"
#include "disp.h"

#define MAX_IDR_SHIFT		(sizeof(int) * 8 - 1)
#define MAX_IDR_BIT		(1U << MAX_IDR_SHIFT)

/* Leave the possibility of an incomplete final layer */
#define MAX_IDR_LEVEL ((MAX_IDR_SHIFT + IDR_BITS - 1) / IDR_BITS)

/* Number of id_layer structs to leave in free list */
#define MAX_IDR_FREE (MAX_IDR_LEVEL * 2)

/* TODO: lock this list */
static LIST_HEAD(backing_devices);
static struct mutex read_bootsector_mutex;

void windrbd_device_error(struct drbd_device *device, const char ** err_str_out, const char *fmt, ...)
{
	char *err_str;
	va_list args;

        va_start(args, fmt);
	err_str = kvasprintf(GFP_ATOMIC, fmt, args);
        va_end(args);

	if (err_str != NULL) {
		drbd_warn(device, "%s\n", err_str);

		/* Do not overwrite existing err strings. */
		if (err_str_out != NULL && *err_str_out == NULL)
			*err_str_out = err_str;
	}	/* else no memory */
}

void msleep(int ms)
{
	LARGE_INTEGER d;

	if (KeGetCurrentIrql() >= DISPATCH_LEVEL) {
		/* Maybe busy loop? */
		printk("msleep called with irqlevel > DISPATCH_LEVEL (%d)\n", KeGetCurrentIrql());
	} else {
		d.QuadPart = -10000LL * ms;
		KeDelayExecutionThread(KernelMode, FALSE, &d);
	}
}

uint64_t roundup(uint64_t x, uint64_t y)
{
	return (((x) + (y - 1)) / y) * y;
}

//__ffs - find first bit in word.
ULONG_PTR __ffs(ULONG_PTR word) 
{
	int num = 0;

#if BITS_PER_LONG == 64
	if ((word & 0xffffffff) == 0) {
		num += 32;
		word >>= 32;
	}
#endif
	if ((word & 0xffff) == 0) {
		num += 16;
		word >>= 16;
	}
	if ((word & 0xff) == 0) {
		num += 8;
		word >>= 8;
	}
	if ((word & 0xf) == 0) {
		num += 4;
		word >>= 4;
	}
	if ((word & 0x3) == 0) {
		num += 2;
		word >>= 2;
	}
	if ((word & 0x1) == 0)
		num += 1;
	return num;
}

#define ffz(x)  __ffs(~(x))

int fls(int x)
{
	int r = 32;

	if (!x)
		return 0;
	if (!(x & 0xffff0000u)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xff000000u)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xf0000000u)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xc0000000u)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000u)) {
		x <<= 1;
		r -= 1;
	}
	return r;
}

#define BITOP_WORD(nr)          ((nr) / BITS_PER_LONG)

ULONG_PTR find_first_bit(const ULONG_PTR* addr, ULONG_PTR size)
{
	const ULONG_PTR* p = addr;
	ULONG_PTR result = 0;
	ULONG_PTR tmp;

	while (size & ~(BITS_PER_LONG - 1)) {
		if ((tmp = *(p++)))
			goto found;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
#ifdef _WIN64
	tmp = (*p) & (~0ULL >> (BITS_PER_LONG - size));
	if (tmp == 0ULL)	{	/* Are any bits set? */
#else
	tmp = (*p) & (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)	{	/* Are any bits set? */
#endif
		return result + size;	/* Nope. */
	}
found:
	return result + __ffs(tmp);
}

ULONG_PTR find_next_bit(const ULONG_PTR *addr, ULONG_PTR size, ULONG_PTR offset)
{
// printk("addr is %p, *addr is %llx size is %lld, offset is %lld\n", addr, *addr, size, offset);
	const ULONG_PTR *p = addr + BITOP_WORD(offset);
	ULONG_PTR result = offset & ~(BITS_PER_LONG - 1);
	ULONG_PTR tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset %= BITS_PER_LONG;
	if (offset) {
		tmp = *(p++);
#ifdef _WIN64
		tmp &= (~0ULL << offset);
#else
		tmp &= (~0UL << offset);
#endif
		if (size < BITS_PER_LONG)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}
	while (size & ~(BITS_PER_LONG - 1)) {
		if ((tmp = *(p++)))
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
#ifdef _WIN64
	tmp &= (~0ULL >> (BITS_PER_LONG - size));
	if (tmp == 0ULL)	/* Are any bits set? */
#else
	tmp &= (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)		/* Are any bits set? */
#endif
		return result + size;	/* Nope. */
found_middle:
	return result + __ffs(tmp);
}

const char _zb_findmap [] = {
    0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
    0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
    0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,
    0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,6,
    0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,
    0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,5,
    0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,
    0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,7,
    0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
    0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
    0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
    0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6,
    0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
    0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
    0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
    0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 8 };

static inline ULONG_PTR __ffz_word(ULONG_PTR nr, ULONG_PTR word)
 {
 #ifdef _WIN64
    if ((word & 0xffffffff) == 0xffffffff) {
            word >>= 32;
            nr += 32;
    }
 #endif
    if ((word & 0xffff) == 0xffff) {
            word >>= 16;
            nr += 16;
    }
    if ((word & 0xff) == 0xff) {
            word >>= 8;
            nr += 8;
    }
	return nr + _zb_findmap[(unsigned char) word];
 }
 /*
 * Find the first cleared bit in a memory region.
 */
ULONG_PTR find_first_zero_bit(const ULONG_PTR *addr, ULONG_PTR size)
 {
	const ULONG_PTR *p = addr;
	ULONG_PTR result = 0;
	ULONG_PTR tmp;

	 while (size & ~(BITS_PER_LONG - 1)) {
		 if (~(tmp = *(p++)))
			 goto found;
		 result += BITS_PER_LONG;
		 size -= BITS_PER_LONG;
	 }
	 if (!size)
		 return result;

#ifdef _WIN64
	 tmp = (*p) | (~0ULL << size);
	 if (tmp == ~0ULL)        /* Are any bits zero? */
#else
	 tmp = (*p) | (~0UL << size);
	 if (tmp == ~0UL)        /* Are any bits zero? */
#endif
		 return result + size;        /* Nope. */
 found:
	 return result + ffz(tmp);
 }

int find_next_zero_bit(const ULONG_PTR * addr, ULONG_PTR size, ULONG_PTR offset)
{
	const ULONG_PTR *p;
	ULONG_PTR bit, set;
 
    if (offset >= size)
            return size;
    bit = offset & (BITS_PER_LONG - 1);
    offset -= bit;
    size -= offset;
    p = addr + offset / BITS_PER_LONG;
    if (bit) {
        /*
        * __ffz_word returns BITS_PER_LONG
        * if no zero bit is present in the word.
        */
        set = __ffz_word(bit, *p >> bit);
        if (set >= size)
                return size + offset;
        if (set < BITS_PER_LONG)
                return set + offset;
        offset += BITS_PER_LONG;
        size -= BITS_PER_LONG;
        p++;
    }

    return offset + find_first_zero_bit(p, size);
 }

static spinlock_t g_test_and_change_bit_lock;

int test_and_change_bit(int nr, const ULONG_PTR *addr)
{
	ULONG_PTR mask = BIT_MASK(nr);
	ULONG_PTR *p = ((ULONG_PTR *) addr);
	ULONG_PTR old;
	KIRQL flags;

	spin_lock_irqsave(&g_test_and_change_bit_lock, flags);
	old = *p;
	*p = old ^ mask;
	spin_unlock_irqrestore(&g_test_and_change_bit_lock, flags);

	return (old & mask) != 0;
}

LONG_PTR xchg(LONG_PTR *target, LONG_PTR value)
{
#ifdef _WIN64
	return (InterlockedExchange64(target, value));
#else
	return (InterlockedExchange(target, value));
#endif
}


void atomic_set(atomic_t *v, int i)
{
	InterlockedExchange((long *)v, i);
}

void atomic_add(int i, atomic_t *v)
{
	InterlockedExchangeAdd((long *)v, i);
}

void atomic_add64(LONGLONG a, atomic_t64 *v)
{
	InterlockedExchangeAdd64((LONGLONG*)v, a);
}

	/* TODO: atomic? Results may be non-monotonic decreasing, not
	 * sure if double values can occur.
	 */
int atomic_add_return(int i, atomic_t *v)
{
	int retval;
	retval = InterlockedExchangeAdd((LONG*)v, i);
	retval += i;
	return retval;
}

void atomic_sub(int i, atomic_t *v)
{
	atomic_sub_return(i, v);
}

void atomic_sub64(LONGLONG a, atomic_t64 *v)
{
	atomic_sub_return64(a, v);
}

	/* TODO: atomic? Results may be non-monotonic decreasing, not
	 * sure if double values can occur.
	 */
int atomic_sub_return(int i, atomic_t *v)
{
	int retval;
	retval = InterlockedExchangeAdd((LONG*)v, -i);
	retval -= i;
	return retval;
}

LONGLONG atomic_sub_return64(LONGLONG a, atomic_t64 *v)
{
	LONGLONG retval;
	retval = InterlockedExchangeAdd64((LONGLONG*)v, -a);
	retval -= a;
	return retval;
}

	/* TODO: this is really atomic? */

int atomic_dec_and_test(atomic_t *v)
{
	return (0 == InterlockedDecrement((LONG*)v));
}

int atomic_sub_and_test(int i, atomic_t *v)
{
	LONG_PTR retval;
	retval = InterlockedExchangeAdd((LONG*)v, -i);
	retval -= i;
	return (retval == 0);
}

int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	return InterlockedCompareExchange((long *)v, new, old);
}

int atomic_xchg(atomic_t *v, int n)
{
	return InterlockedExchange((LONG*)v, n);
}

int atomic_read(const atomic_t *v)
{
	return InterlockedAnd((LONG*)v, 0xffffffff);
}

LONGLONG atomic_read64(const atomic_t64 *v)
{
	return InterlockedAnd64((LONGLONG*)v, 0xffffffffffffffff);
}

#ifndef KMALLOC_DEBUG

	/* TODO: we would save patches to DRBD if we skip the tag
	   here .. aren't using Windows Degugger anyway at the moment..
	 */
	/* TODO: honor the flag: alloc from PagedPool if flag is GFP_USER */

void *kmalloc(int size, int flag, ULONG Tag)
{
	return ExAllocatePoolUninitialized(NonPagedPool, size, Tag);
}

void *kcalloc(int size, int count, int flag, ULONG Tag)
{
	/* TODO: flag is 0? */
	return kzalloc(size*count, 0, Tag);
}

void *kzalloc(int size, int flag, ULONG Tag)
{
	void *mem;

	mem = kmalloc(size, flag, Tag);
	if (mem != NULL)
		RtlZeroMemory(mem, size);

	return mem;
}

#endif

char *kstrdup(const char *s, int gfp)
{
	size_t len;
	char *buf;

	if (!s)
		return NULL;

	len = strlen(s) + 1;
	buf = kmalloc(len, gfp, 'C3DW');
	if (buf)
		memcpy(buf, s, len);
	return buf;
}

/**
 * strlcpy - Copy a C-string into a sized buffer
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @size: size of destination buffer
 *
 * Compatible with ``*BSD``: the result is always a valid
 * NUL-terminated string that fits in the buffer (unless,
 * of course, the buffer size is zero). It does not pad
 * out the result like strncpy() does.
 */
size_t strlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t len = (ret >= size) ? size - 1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}

void *page_address(const struct page *page)
{
		/* This happens sometimes in drbd_drop_unsent() */
	if (page == NULL)
		return NULL;

	return page->addr;
}

#ifdef KMALLOC_DEBUG

struct page *alloc_page_of_size_debug(int flag, size_t size, const char *file, int line, const char *func)
{
		/* Round up to the next PAGE_SIZE */

	BUG_ON(size==0);
	size = (((size-1) / PAGE_SIZE)+1)*PAGE_SIZE;

	struct page *p = kzalloc_debug(sizeof(struct page), 0, file, line, func);
	if (!p)	{
		printk("alloc_page struct page failed\n");
		return NULL;
	}

		/* Under Windows this is defined to align to a page
		 * of PAGE_SIZE bytes if size is >= PAGE_SIZE.
		 * PAGE_SIZE itself is always 4096 under Windows.
		 */

	p->addr = kmalloc_debug(size, 0, file, line, func);
	if (!p->addr)	{
		kfree_debug(p, file, line, func); 
		printk("alloc_page failed (size is %d)\n", size);
		return NULL;
	}
	kref_init(&p->kref);
	p->size = size;

// printk("allocating page %p page->addr is %p page->size is %d from %s:%d (%s)\n", p, p->addr, p->size, file, line, func);
	return p;
}

struct page *alloc_page_debug(int flag, const char *file, int line, const char *func)
{
	return alloc_page_of_size_debug(flag, PAGE_SIZE, file, line, func);
}

void __free_page_debug(struct page *page, const char *file, int line, const char *func)
{
// printk("freeing page %p page->addr is %p from %s:%d (%s)\n", page, page->addr, file, line, func);
	if (!page->is_system_buffer)
		kfree_debug(page->addr, file, line, func);

	kfree_debug(page, file, line, func); 
}


void free_page_kref_debug(struct kref *kref, const char *file, int line, const char *func)
{
	struct page *page = container_of(kref, struct page, kref);
	__free_page_debug(page, file, line, func);
}

#undef free_page_kref

	/* This is used as a function pointer parameter to put_page,
	 * its signature cannot be changed since the number of
	 * parameters is fixed.
	 */

void free_page_kref(struct kref *kref)
{
	struct page *page = container_of(kref, struct page, kref);
	__free_page_debug(page, __FILE__, __LINE__, __func__);
}

#else

struct page *alloc_page_of_size(int flag, size_t size)
{
		/* Round up to the next PAGE_SIZE */

	BUG_ON(size==0);
	size = (((size-1) / PAGE_SIZE)+1)*PAGE_SIZE;

	struct page *p = kzalloc(sizeof(struct page),0, 'D3DW');
	if (!p)	{
		printk("alloc_page struct page failed\n");
		return NULL;
	}

		/* Under Windows this is defined to align to a page
		 * of PAGE_SIZE bytes if size is >= PAGE_SIZE.
		 * PAGE_SIZE itself is always 4096 under Windows.
		 */

	p->addr = kmalloc(size, 0, 'E3DW');
	if (!p->addr)	{
		kfree(p); 
		printk("alloc_page failed (size is %d)\n", size);
		return NULL;
	}
	kref_init(&p->kref);
	p->size = size;

	return p;
}

struct page *alloc_page(int flag)
{
	return alloc_page_of_size(flag, PAGE_SIZE);
}

void __free_page(struct page *page)
{
	if (!page->is_system_buffer)
		kfree(page->addr);
	kfree(page); 
}

void free_page_kref(struct kref *kref)
{
zak
	struct page *page = container_of(kref, struct page, kref);
	__free_page(page);
}

#endif

#ifndef KMALLOC_DEBUG

void kfree(const void * x)
{
	if (x)
		ExFreePool((void*)x);
}

void kvfree(const void * x)
{
	if (x)
		ExFreePool((void*)x);
}

int dump_memory_allocations(int free_them)
{
	printk("Cannot dump memory allocations: to enable recompile with KMALLOC_DEBUG defined.\n");
	return -1;
}

#endif

#ifdef KREF_DEBUG
// from  linux 2.6.32
int kref_put_debug(struct kref *kref, void (*release)(struct kref *kref), const char *release_name, const char *file, int line, const char *func, int may_printk)
{
	WARN_ON(release == NULL);
	WARN_ON(release == (void (*)(struct kref *))kfree);

	if (may_printk)
		printk("kref_put %p from %s:%d %s() release function is %s() refcnt is %d\n", kref, file, line, func, release_name, atomic_read(&kref->refcount.refs));

	if (atomic_dec_and_test(&kref->refcount.refs))
	{
		if (may_printk)
			printk("About to release object %p via %s\n", kref, release_name);
		release(kref);
		if (may_printk)
			printk("Released object %p via %s\n", kref, release_name);
		return 1;
	}
	return 0;
}

void kref_get_debug(struct kref *kref, const char *file, int line, const char *func, int may_printk)
{
	atomic_inc(&kref->refcount.refs);

	if (may_printk)
		printk("kref_get on object %p ref is now (after inc) %d, called from %s:%d (%s())\n", kref, atomic_read(&kref->refcount.refs), file, line, func);
}

void kref_init_debug(struct kref *kref, const char *file, int line, const char *func)
{
	atomic_set(&kref->refcount.refs, 1);
	printk("kref_init on object %p ref is now %d, called from %s:%d (%s())\n", kref, atomic_read(&kref->refcount.refs), file, line, func);
}


#else

// from  linux 2.6.32
int kref_put(struct kref *kref, void (*release)(struct kref *kref))
{
	WARN_ON(release == NULL);
	WARN_ON(release == (void (*)(struct kref *))kfree);

	if (atomic_dec_and_test(&kref->refcount.refs))
	{
		release(kref);
		return 1;
	}
	return 0;
}

void kref_get(struct kref *kref)
{
	atomic_inc(&kref->refcount.refs);
}

void kref_init(struct kref *kref)
{
	atomic_set(&kref->refcount.refs, 1);
}

#endif

struct request_queue *bdev_get_queue(struct block_device *bdev)
{
	if (bdev && bdev->bd_disk)
		return bdev->bd_disk->queue;

	return NULL;
}

	/* This probably never gets implemented since we do
	 * not have auto promote and Windows caches at file
	 * system level, not at block device level.
	 */

int fsync_bdev(struct block_device *bdev)
{
	printk("function fsync_bdev not implemented\n");
	return 0;
}

static struct bio *bio_alloc_ll(gfp_t gfp_mask, int nr_iovecs, ULONG Tag)
{
	struct bio *bio;

	bio = kzalloc(sizeof(struct bio) + nr_iovecs * sizeof(struct bio_vec), gfp_mask, Tag);
	if (!bio)
	{
		return 0;
	}
	bio->bi_max_vecs = nr_iovecs;
	bio->bi_cnt = 1;
	bio->bi_vcnt = 0;
	spin_lock_init(&bio->device_failed_lock);

	return bio;
}

#ifdef BIO_ALLOC_DEBUG

struct bio *bio_alloc_debug(gfp_t mask, int nr_iovecs, ULONG tag, char *file, int line, char *func)
{
	struct bio *bio = bio_alloc_ll(mask, nr_iovecs, tag);

	if (bio) {
		bio->file = file;
		bio->line = line;
		bio->func = func;
	}
	return bio;
}

#else

struct bio *bio_alloc(gfp_t gfp_mask, int nr_iovecs, ULONG Tag)
{
	return bio_alloc_ll(gfp_mask, nr_iovecs, Tag);
}

#endif

	/* biosets are meant for being responsive under memory pressure.
	 * we haven't implemented it yet because windows fails earlier
	 * on low memory.
	 */

struct bio *bio_alloc_bioset(gfp_t gfp_mask, int nr_iovecs, struct bio_set *unused)
{
	return bio_alloc(gfp_mask, nr_iovecs, 'DRBD');
}

static void free_mdl_chain_and_irp(struct _IRP *irp)
{
	struct _MDL *mdl, *next_mdl;

	for (mdl = irp->MdlAddress;
	     mdl != NULL;
	     mdl = next_mdl) {
		next_mdl = mdl->Next;
		if (mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA)
			MmUnmapLockedPages(mdl->MappedSystemVa, mdl);

		if (mdl->MdlFlags & MDL_PAGES_LOCKED)
			MmUnlockPages(mdl);

		IoFreeMdl(mdl);
	}
	irp->MdlAddress = NULL;

	IoFreeIrp(irp);
}

static void free_mdls_and_irp(struct bio *bio)
{
	int r;

		/* This happens quite frequently when DRBD allocates a
	         * bio without ever calling generic_make_request on it.
		 */

	if (bio->bi_irps == NULL)
		return;

	for (r=0;r<bio->bi_num_requests;r++) {
		if (bio->bi_irps[r] == NULL)
			continue;

		/* This has to be done before freeing the buffers with
		 * __free_page(). Else we get a PFN list corrupted (or
		 * so) BSOD.
		 */

		free_mdl_chain_and_irp(bio->bi_irps[r]);
	}

	kfree(bio->bi_irps);
	bio->bi_irps = NULL;
}

void bio_get_debug(struct bio *bio, const char *file, int line, const char *func)
{
	int cnt;
	cnt = atomic_inc(&bio->bi_cnt);
}

void bio_put_debug(struct bio *bio, const char *file, int line, const char *func)
{
	int cnt;
	cnt = atomic_dec(&bio->bi_cnt);
	if (cnt == 0)
		bio_free(bio);
}

#ifndef BIO_REF_DEBUG

void bio_put(struct bio *bio)
{
	int cnt;
	cnt = atomic_dec(&bio->bi_cnt);
	if (cnt == 0)
		bio_free(bio);
}

#endif

static LIST_HEAD(bios_to_be_freed_list);
static LIST_HEAD(bios_to_be_freed_list2);
static spinlock_t bios_to_be_freed_lock;
static struct wait_queue_head bios_to_be_freed_event;
static bool free_bios_thread_should_run;
static struct task_struct *free_bios_thread;

	/* That just puts to bio on the to_be_freed list. Reason
	 * is that free_mdl...() must not run at IRQL > PASSIVE_LEVEL
	 */

void bio_free(struct bio *bio)
{
	KIRQL flags;
	int i;
	struct bio *upper_bio = bio->is_cloned_from;

		/* DRBD considers pages here as not in use any more.
		 * however we still have MDLs referencing memory
		 * of the page. So take a reference here and drop
		 * it as soon as MDLs are freed.
		 */

	spin_lock_irqsave(&bios_to_be_freed_lock, flags);
	list_add(&bio->to_be_freed_list, &bios_to_be_freed_list);
	spin_unlock_irqrestore(&bios_to_be_freed_lock, flags);

	if (upper_bio != NULL)
		bio_put(upper_bio);

	wake_up(&bios_to_be_freed_event);
}

	/* Since freeing parts of MDLs must happen at IRQL = PASSIVE_LEVEL
	 * (for user space allocated data) we use this thread to free
	 * the IRPs.
	 */

static int free_bios_thread_fn(void *unused)
{
	KIRQL flags;
	struct bio *bio, *bio2;
	int i;

	while (1) {
		wait_event(bios_to_be_freed_event, !list_empty(&bios_to_be_freed_list) || !free_bios_thread_should_run);
		if (!free_bios_thread_should_run)
			break;

		spin_lock_irqsave(&bios_to_be_freed_lock, flags);
		list_for_each_entry_safe(struct bio, bio, bio2, &bios_to_be_freed_list, to_be_freed_list) {
			list_del(&bio->to_be_freed_list);
			list_add(&bio->to_be_freed_list2, &bios_to_be_freed_list2);
		}
		spin_unlock_irqrestore(&bios_to_be_freed_lock, flags);

		list_for_each_entry_safe(struct bio, bio, bio2, &bios_to_be_freed_list2, to_be_freed_list2) {
			list_del(&bio->to_be_freed_list2);
			free_mdls_and_irp(bio);
// printk("out of free_mdls_and_irp(%p) page is %p page refcount is %d\n", bio, bio->bi_io_vec[0].bv_page, refcount_read(&bio->bi_io_vec[0].bv_page->kref.refcount));
			for (i=0;i<bio->bi_vcnt;i++) {
				put_page(bio->bi_io_vec[i].bv_page);
			}
			if (bio->patched_bootsector_buffer != NULL)
				kfree(bio->patched_bootsector_buffer);

			if (bio->bi_big_buffer != NULL)
				kfree(bio->bi_big_buffer);

			if (bio->delayed_io_completion && bio->bi_upper_irp != NULL)
				IoCompleteRequest(bio->bi_upper_irp, bio->bi_upper_irp->IoStatus.Status != STATUS_SUCCESS ? IO_NO_INCREMENT : IO_DISK_INCREMENT);

			kfree(bio);
		}
	}

	return 0;
}

void init_free_bios(void)
{
	spin_lock_init(&bios_to_be_freed_lock);
	init_waitqueue_head(&bios_to_be_freed_event);
	free_bios_thread_should_run = true;
	free_bios_thread = kthread_run(free_bios_thread_fn, NULL, "free_bios");
	if (free_bios_thread == NULL)
		printk("Warning: could not start free_bios thread, bios will not be freed.\n");
}

	/* Call this only when driver should be unloaded. */

void shutdown_free_bios(void)
{
	free_bios_thread_should_run = false;
	wake_up(&bios_to_be_freed_event);

		/* Let thread reaper do the rest */
}

struct bio *bio_clone(struct bio * bio_src, int flag)
{
	struct bio *bio = bio_alloc(flag, bio_src->bi_max_vecs, '24DW');
	int i;

	if (bio == NULL)
		return NULL;

	memcpy(bio->bi_io_vec, bio_src->bi_io_vec, bio_src->bi_max_vecs * sizeof(struct bio_vec));
	bio->bi_iter.bi_sector = bio_src->bi_iter.bi_sector;
	bio->bi_bdev = bio_src->bi_bdev;
	//bio->bi_flags |= 1 << BIO_CLONED;
	bio->bi_opf = bio_src->bi_opf;
	bio->bi_vcnt = bio_src->bi_vcnt;
	bio->bi_iter.bi_size = bio_src->bi_iter.bi_size;
	bio->bi_iter.bi_idx = bio_src->bi_iter.bi_idx;
	bio->bi_num_requests = bio_src->bi_num_requests;
	bio->bi_this_request = bio_src->bi_this_request;

	for (i=0;i<bio->bi_vcnt;i++) {
		get_page(bio->bi_io_vec[i].bv_page);
	}

#ifdef BIO_ALLOC_DEBUG
	bio->file = bio_src->file;
	bio->line = bio_src->line;
	bio->func = bio_src->func;
#endif

	/* Take a reference to the original bio. This will be dropped
	   when the cloned bio is freed. The reason for this is that
	   we need to wait for all lower writes to complete and be
	   fully freed (especially the MDLs unmapped) before we
	   can complete the upper request, else PTE BSOD in NTFS (or
	   whatever).
	*/

	bio_get(bio_src);
	bio->is_cloned_from = bio_src;

	return bio;
}

int bio_add_page_debug(struct bio *bio, struct page *page, unsigned int len,unsigned int offset, char *file, int line, char *func)
{
	struct bio_vec *bvec = &bio->bi_io_vec[bio->bi_vcnt++];

	bvec->bv_page = page;
	bvec->bv_len = len;
	bvec->bv_offset = offset;
	bio->bi_iter.bi_size += len;

		/* for put_page in free_bios_thread_fn() */
	get_page(page);

	return len;
}

#include "drbd_int.h"

LONG_PTR IS_ERR_OR_NULL(const void *ptr)
{
	return !ptr || IS_ERR_VALUE((LONG_PTR) ptr); 
}

void *ERR_PTR(LONG_PTR error)
{
	return (void *) error;
}

LONG_PTR PTR_ERR(const void *ptr)
{
	return (LONG_PTR)ptr;
}

LONG_PTR IS_ERR(void *ptr)
{
	return IS_ERR_VALUE((LONG_PTR) ptr);
}

void init_completion_debug(struct completion *completion, const char *file, int line, const char *func)
{
// printk("from %s:%d (%s()) completion is %p\n", file, line, func, completion);
	init_waitqueue_head(&completion->wait);
	completion->completed = false;
}

ULONG_PTR wait_for_completion_timeout_debug(struct completion *completion, ULONG_PTR timeout, const char *file, int line, const char *func)
{
	ULONG_PTR ret;

// printk("from %s:%d (%s()) completion is %p\n", file, line, func, completion);
// printk("into wait_event %p ...\n", completion);
		/*  Not interruptible. When this is interruptible BSODs
		 *  on disonnect may happen. */
	wait_event_timeout(ret, completion->wait, completion->completed, timeout);
// printk("out of wait_event %p ret is %d...\n", completion, ret);

	return ret;
}

void wait_for_completion_debug(struct completion *completion, const char *file, int line, const char *func)
{
// printk("from %s:%d (%s()) completion is %p\n", file, line, func, completion);
	wait_for_completion_timeout(completion, MAX_SCHEDULE_TIMEOUT);
}

void complete_debug(struct completion *c, const char *file, int line, const char *func)
{
// printk("from %s:%d (%s()) completion is %p\n", file, line, func, c);
// printk("completing %p\n", c);
	c->completed = true;
	wake_up(&c->wait);
// printk("%p completed\n", c);
}

void complete_all_debug(struct completion *c, const char *file, int line, const char *func)
{
// printk("from %s:%d (%s()) completion is %p\n", file, line, func, c);
// printk("completing all %p\n", c);
	c->completed = true;
	wake_up_all(&c->wait);
// printk("%p completed\n", c);
}

struct workqueue_struct *system_wq;

void queue_work(struct workqueue_struct *queue, struct work_struct *work)
{
	KIRQL flags, flags2;

	if (queue->about_to_destroy) {
		printk("Warning: Attempt to queue_work while destroying workqueue\n");
		return;
	}
	spin_lock_irqsave(&queue->work_list_lock, flags2);
	spin_lock_irqsave(&work->pending_lock, flags);

	if (work->pending) {
		spin_unlock_irqrestore(&work->pending_lock, flags);
		spin_unlock_irqrestore(&queue->work_list_lock, flags2);
		if (queue != work->orig_queue || work->orig_func != work->func)
			printk("work %p pending on queue %s: queue or func have changed: queue is %p (%s) work->orig_queue is %p (%s) work->orig_func is %p work->func is %p\n", queue, queue->name, work->orig_queue, work->orig_queue->name, work->orig_func, work->func);

		return;
	}
	work->pending = 1;

	work->orig_queue = queue;
	work->orig_func = work->func;

	list_add_tail(&work->work_list, &queue->work_list);
	spin_unlock_irqrestore(&work->pending_lock, flags);
	spin_unlock_irqrestore(&queue->work_list_lock, flags2);

		/* signal to run_singlethread_workqueue */
	KeSetEvent(&queue->wakeupEvent, 0, FALSE);
}

static int run_singlethread_workqueue(struct workqueue_struct* wq)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PVOID waitObjects[2] = { &wq->wakeupEvent, &wq->killEvent };
	int maxObj = 2;
	struct work_struct *w;
	KIRQL flags, flags2;

	while (wq->run) {
		status = KeWaitForMultipleObjects(maxObj, &waitObjects[0], WaitAny, Executive, KernelMode, FALSE, NULL, NULL);

		switch (status) {
		case STATUS_WAIT_0:
			while (1) {
				spin_lock_irqsave(&wq->work_list_lock, flags);
				if (list_empty(&wq->work_list)) {
					spin_unlock_irqrestore(&wq->work_list_lock, flags);
					break;
				}
				w = list_first_entry(&wq->work_list, struct work_struct, work_list);
				spin_lock_irqsave(&w->pending_lock, flags2);
				list_del(&w->work_list);

		/* If we do this after calling func, it hangs in Disconnecting
		 * (or disk Failed) state forever ... Update: no this also
		 * happens when this is after calling func. Must be something
		 * else ... If this is here we get use after free.
		 */
				w->pending = 0;
				spin_unlock_irqrestore(&w->pending_lock, flags2);
				spin_unlock_irqrestore(&wq->work_list_lock, flags);

				if (wq->about_to_destroy) {
					printk("About to destroy workqueue %s not calling function\n", wq->name);
				} else {
// printk("into func\n");
					w->func(w);
				}
			}
			KeSetEvent(&wq->workFinishedEvent, 0, FALSE);
			break;

		case STATUS_WAIT_1:
			wq->run = FALSE;
			break;
		}
	}
	KeSetEvent(&wq->readyToFreeEvent, 0, FALSE);
	return 0;
}

struct workqueue_struct *alloc_ordered_workqueue(const char * fmt, int flags, ...)
{
	struct workqueue_struct *wq;
	va_list args;

	wq = kzalloc(sizeof(*wq), 0, '31DW');
	if (wq == NULL) {
		printk("Warning: not enough memory for workqueue\n");
		return NULL;
	}

	KeInitializeEvent(&wq->wakeupEvent, SynchronizationEvent, FALSE);
	KeInitializeEvent(&wq->killEvent, SynchronizationEvent, FALSE);
	KeInitializeEvent(&wq->workFinishedEvent, SynchronizationEvent, FALSE);
	KeInitializeEvent(&wq->readyToFreeEvent, SynchronizationEvent, FALSE);

	INIT_LIST_HEAD(&wq->work_list);
	spin_lock_init(&wq->work_list_lock);
	wq->about_to_destroy = 0;

	va_start(args, flags);
		/* ignore error if string is too long */
	(void) RtlStringCbVPrintfA(wq->name, sizeof(wq->name)-1, fmt, args);
	wq->name[sizeof(wq->name)-1] = '\0';
	va_end(args);

	wq->run = TRUE;

	wq->thread = kthread_create(run_singlethread_workqueue, wq, "wq_%s", wq->name);
	if (IS_ERR(wq->thread)) {
		printk("kthread_run failed on creating workqueue thread, err is %d\n", PTR_ERR(wq->thread));
		kfree(wq);
		return NULL;
	}
	wake_up_process(wq->thread);

	return wq;
}

/* This should ensure that all work on the workqueue is done (has finished).
 * It is typically invoked when a driver shuts down a resource (for example
 * on drbdadm down).
 */
void flush_workqueue(struct workqueue_struct *wq)
{
	PVOID waitObjects[2] = { &wq->workFinishedEvent, &wq->killEvent };
	NTSTATUS status;

	KeResetEvent(&wq->workFinishedEvent);
	KeSetEvent(&wq->wakeupEvent, 0, FALSE);
	status = KeWaitForMultipleObjects(2, &waitObjects[0], WaitAny, Executive, KernelMode, FALSE, NULL, NULL);
	if (!list_empty(&wq->work_list)) {
		printk("Warning: wq->work_list not empty at exiting flush_workqueue\n");
	}
}

void destroy_workqueue(struct workqueue_struct *wq)
{
// printk("1 wq is %s\n", wq->name);
	wq->about_to_destroy = 1;
// printk("2 wq is %s\n", wq->name);
	flush_workqueue(wq);
// printk("3 wq is %s\n", wq->name);
	KeSetEvent(&wq->killEvent, 0, FALSE);
// printk("4 wq is %s\n", wq->name);
	KeWaitForSingleObject(&wq->readyToFreeEvent, Executive, KernelMode, FALSE, NULL);
// printk("5 wq is %s\n", wq->name);

	kfree(wq);
// printk("6 wq is %s\n", wq->name);
}

int threads_sleeping;

void enter_interruptible_debug(const char *file, int line, const char *func)
{
// cond_printk("Thread %s entering interruptible sleep (from %s:%d (%s()).\n", current->comm, file, line, func);
	threads_sleeping++;
}

void exit_interruptible_debug(const char *file, int line, const char *func)
{
// cond_printk("Thread %s exiting interruptible sleep. (from %s:%d (%s())\n", current->comm, file, line, func);
	threads_sleeping--;
}

void get_random_bytes(char *buf, int nbytes)
{
    static ULONG_PTR lcg_2_64_div_pi = 0;
    static ULONG_PTR mmix_knuth = 0;
    LARGE_INTEGER li;
    ULONG_PTR rnt;
    ULONG rn=0;	// TODO: KeQueryPerformanceCounter
    int length;

    if (mmix_knuth == 0) {
	    li = KeQueryPerformanceCounter(NULL);
	    mmix_knuth = li.QuadPart;
    }
    if (lcg_2_64_div_pi == 0) {
	    lcg_2_64_div_pi = mmix_knuth;
	    get_random_bytes((char*)&lcg_2_64_div_pi, sizeof(lcg_2_64_div_pi));
    }

    /* https://www.winvistatips.com/threads/verifier-exe-is-not-compatible-with-rtlrandom-ex.193117/ */
    if (KeGetCurrentIrql() < DISPATCH_LEVEL) {
	    /* RtlRandomEx only returns [0, 2^31-1], so not even 31bit. */
	    RtlRandomEx(&rn);
	    mmix_knuth += rn;
    }

    while (nbytes > 0)
    {
	    /* https://en.wikipedia.org/wiki/Linear_congruential_generator */
	    mmix_knuth = mmix_knuth * 6364136223846793005 + 1442695040888963407;
	    /* Nearest prime to 2^64/π */
	    lcg_2_64_div_pi = lcg_2_64_div_pi * 5871781006564002809 + 1;

	    /* Hide lowest bits */
	    rn = (lcg_2_64_div_pi ^ mmix_knuth) >> 11;

	    length = sizeof(rn);
	    if (length > nbytes)
		    length = nbytes;

	    memcpy(buf, (UCHAR *)&rn, length);

	    nbytes -= length;
	    buf += length;
    }
}

unsigned int crypto_tfm_alg_digestsize(struct crypto_tfm *tfm)
{
	return 4; // 4byte in constant
}

int page_count(struct page *page)
{
	return atomic_read(&page->kref.refcount.refs);
}

static void timer_callback(PKDPC dpc, struct timer_list* timer, PVOID arg1, PVOID arg2)
{
	(void)arg1;
	(void)arg2;
	(void)dpc;
	timer->function(timer);
}

void timer_setup(struct timer_list *timer, void(*callback)(struct timer_list *timer), ULONG_PTR flags_unused)
{
	timer->function = callback;
	KeInitializeTimer(&timer->ktimer);
	KeInitializeDpc(&timer->dpc, (PKDEFERRED_ROUTINE)timer_callback, timer);
}

void add_timer(struct timer_list *t)
{
	mod_timer(t, t->expires);
}

void del_timer(struct timer_list *t)
{
	KeCancelTimer(&t->ktimer);
    t->expires = 0;
}

/**
 * timer_pending - is a timer pending?
 * @timer: the timer in question
 *
 * timer_pending will tell whether a given timer is currently pending,
 * or not. Callers must ensure serialization wrt. other operations done
 * to this timer, eg. interrupt contexts, or other CPUs on SMP.
 *
 * return value: 1 if the timer is pending, 0 if not.
 */
static __inline int timer_pending(const struct timer_list * timer)
{
    return timer->ktimer.Header.Inserted;
}


	/* TODO: sync? */

int del_timer_sync(struct timer_list *t)
{
	bool pending = 0;
	pending = timer_pending(t);
	
	del_timer(t);

	return pending;
}


static int __mod_timer(struct timer_list *timer, ULONG_PTR expires, bool pending_only)
{
	int pending = timer_pending(timer);

	if (!pending && pending_only)
		return 0;

	LARGE_INTEGER nWaitTime = { .QuadPart = 0 };
	ULONG_PTR current_milisec = jiffies;

	timer->expires = expires;

	if (current_milisec >= expires)
		nWaitTime.QuadPart = -1;
	else {
		expires -= current_milisec;
		nWaitTime.QuadPart = RELATIVE(MILLISECONDS(expires));
	}

/*
  printk("%s timer(0x%p) current(%d) expires(%d) gap(%d) nWaitTime(%lld)\n",
        timer->name, timer, current_milisec, timer->expires, timer->expires - current_milisec, nWaitTime.QuadPart);
*/

	KeSetTimer(&timer->ktimer, nWaitTime, &timer->dpc);
	return pending;
}

/**
 * mod_timer_pending - modify a pending timer's timeout
 * @timer: the pending timer to be modified
 * @expires: new timeout in jiffies
 *
 * mod_timer_pending() is the same for pending timers as mod_timer(),
 * but will not re-activate and modify already deleted timers.
 *
 * It is useful for unserialized use of timers.
 */
int mod_timer_pending(struct timer_list *timer, ULONG_PTR expires)
{
	return __mod_timer(timer, expires, true);
}

int mod_timer(struct timer_list *timer, ULONG_PTR expires)
{
	return __mod_timer(timer, expires, false);
}

void kobject_put(struct kobject *kobj)
{
    if (kobj) 
    {
        if (kobj->name == NULL)
        {
            return;
        }

		if (atomic_sub_and_test(1, &kobj->kref.refcount.refs))
		{
			void(*release)(struct kobject *kobj);
			release = kobj->ktype->release;
			if (release == 0)
			{
				return;
			}
			release(kobj);
		}
    }
    else
    {
        return;
    }
}

void kobject_del(struct kobject *kobj)
{
    if (!kobj)
    {
        return;
    }
    kobject_put(kobj->parent); 
}

void kobject_get(struct kobject *kobj)
{
    if (kobj)
    {
        kref_get(&kobj->kref);
    }
    else
    {
        return;
    }
}

void del_gendisk(struct gendisk *disk)
{
#if 0
        if (disk != NULL) {
		printk("freeing disk %p\n", disk);
                if (disk->queue != NULL)
                        blk_cleanup_queue(disk->queue);
                put_disk(disk);
        }
#endif
}

//Linux/block/genhd.c
void set_disk_ro(struct gendisk *disk, int flag)
{

}

int signal_pending(struct task_struct *task)
{
	if (task->has_sig_event)
	{
		if (task->sig || KeReadStateEvent(&task->sig_event))
			return 1;
	}
	return 0;
}

void force_sig(int sig, struct task_struct *task)
{
		/* TODO: We need to protect against thread
		 * suddenly dying here. */

	if (task && task->has_sig_event)
	{
		dbg("sending signal %d to task %p (%s)\n", sig, task, task->comm);
		task->sig = sig;
		KeSetEvent(&task->sig_event, 0, FALSE);
	}
}

void send_sig(int sig, struct task_struct *task, int priv)
{
	force_sig(sig, task);
}

void flush_signals(struct task_struct *task)
{
		/* TODO: protect against thread being deleted. */

	if (task && task->has_sig_event)
	{
		dbg("clearing signal event from task %p (%s)\n", task, task->comm);
		KeClearEvent(&task->sig_event); 
		task->sig = 0;
	}
}

static inline blk_status_t win_status_to_blk_status(NTSTATUS status)
{
	return (status == STATUS_SUCCESS) ? 0 : BLK_STS_IOERR; 
}

	/* Patch boot sector contained in buffer.
	 * When to_fs is nonzero, patch from DRBD to filesystem (such as
	 * NTFS) done typically in a read request, else the other way
	 * around. 
	 * When test_mode is non-zero do not touch the buffer. Just
	 * return non-zero when signature is found. To check for
	 * Windows filesystems use patch_boot_sector(buf, 0, 1).
	 *
	 * Returns non-zero if signature is found, else zero.
	 */

static int patch_boot_sector(char *buffer, int to_fs, int test_mode)
{
	static const char *fs_signatures[][2] = {
		{ "NTFS", "DRBD" },
		{ "ReFS", "ReDR" },
		{ "MSDOS5.0", "FATDRBD" },
		{ "EXFAT", "EDRBD" },
		{ NULL, NULL }};
	int fs;
	int i;

	for (fs=0; fs_signatures[fs][0] != NULL; fs++) {
		for (i=0;fs_signatures[fs][to_fs][i] != '\0';i++) {
			if (buffer[3+i] != fs_signatures[fs][to_fs][i])
				break;
		}
		if (fs_signatures[fs][to_fs][i] == '\0') {
			if (!test_mode) {
				printk("Patching boot sector from %s to %s\n", fs_signatures[fs][to_fs], fs_signatures[fs][!to_fs]);
				for (i=0;fs_signatures[fs][to_fs][i] != '\0';i++) {
					buffer[3+i] = fs_signatures[fs][!to_fs][i];
				}
			} else {
				printk("File system signature %s found in boot sector\n", fs_signatures[fs][to_fs]);
			}
			return 1;
		}
	}
	return 0;
}

int is_filesystem(char *buf)
{
	return patch_boot_sector(buf, 0, 1);
}

static int inject_faults(int after, struct fault_injection *i)
{
	if (after >= 0) {
		i->nr_requests_to_failure = after;
		i->nr_requests = 0;
	} else {
		i->nr_requests_to_failure = -1;
		i->nr_requests = -1;
	}
	return 0;
}

	/* This returns non-zero status code if fault should be injected */

static int test_inject_faults(struct fault_injection *i, const char *msg)
{
	if (i->nr_requests >= 0 && i->nr_requests_to_failure >= 0) {
		++i->nr_requests;

		if (i->nr_requests > i->nr_requests_to_failure) {
			printk("Injecting fault after %d requests completed (nr_requests_to_failure is %d) (%s).\n", i->nr_requests, i->nr_requests_to_failure, msg);
			return 1;
		} else if (i->nr_requests+10 > i->nr_requests_to_failure) {
			printk("Will soon inject fault on completion (nr_requests_to_failure is %d, nr_requests_on_completion is %d)\n", i->nr_requests_to_failure, i->nr_requests);
		}
	}
	return 0;
}

static struct fault_injection inject_on_completion = { -1, -1 };
static struct fault_injection inject_on_request = { -1, -1 };

int windrbd_inject_faults(int after, enum fault_injection_location where, struct block_device *windrbd_bdev)
{
	struct block_device *bdev = NULL;

	if (where == ON_META_DEVICE_ON_REQUEST || where == ON_META_DEVICE_ON_COMPLETION) {
		if (windrbd_bdev && windrbd_bdev->drbd_device && windrbd_bdev->drbd_device->ldev)
			bdev = windrbd_bdev->drbd_device->ldev->md_bdev;
	}
	if (where == ON_BACKING_DEVICE_ON_REQUEST || where == ON_BACKING_DEVICE_ON_COMPLETION) {
		if (windrbd_bdev && windrbd_bdev->drbd_device && windrbd_bdev->drbd_device->ldev)
			bdev = windrbd_bdev->drbd_device->ldev->backing_bdev;
	}

	switch (where) {
        case ON_ALL_REQUESTS_ON_REQUEST:
		return inject_faults(after, &inject_on_request);
	case ON_ALL_REQUESTS_ON_COMPLETION:
		return inject_faults(after, &inject_on_completion);
	case ON_META_DEVICE_ON_REQUEST:
        case ON_BACKING_DEVICE_ON_REQUEST:
		if (bdev == NULL) return -1;
		return inject_faults(after, &bdev->inject_on_request);
        case ON_META_DEVICE_ON_COMPLETION:
        case ON_BACKING_DEVICE_ON_COMPLETION:
		if (bdev == NULL) return -1;
		return inject_faults(after, &bdev->inject_on_completion);
	}
	return -1;
}

	/* TODO: is this still needed now that we fixed the interruptible
	 * wait_event issues? That would save a patch in drbd_receiver.c
	 */

int wait_for_bios_to_complete(struct block_device *bdev)
{
	int timeout;

	if (atomic_read(&bdev->num_bios_pending) > 0) {
		dbg("%d bios pending before wait_event\n", atomic_read(&bdev->num_bios_pending));
		dbg("%d IRPs pending before wait_event\n", atomic_read(&bdev->num_irps_pending));
	}
	wait_event_timeout(timeout, bdev->bios_event, (atomic_read(&bdev->num_bios_pending) == 0), HZ*10);
	if (timeout == 0) {
		printk("Warning: Still %d bios and %d IRPs pending after 10 seconds\n", atomic_read(&bdev->num_bios_pending), atomic_read(&bdev->num_irps_pending));
		msleep(1000);
			/* probably BSODs here ... */
	}
	return 0;
}

NTSTATUS DrbdIoCompletion(
  _In_     PDEVICE_OBJECT DeviceObject,
  _In_     PIRP           Irp,
  _In_opt_ PVOID          Context
)
{
/* TODO: Device object is NULL here. Fix that in case we need it one day. */

	struct bio *bio = Context;
	struct _IO_STACK_LOCATION *stack_location = IoGetNextIrpStackLocation (Irp);
	int i;
	NTSTATUS status = Irp->IoStatus.Status;
	KIRQL flags;
	bool one_big_request;

	atomic_dec(&bio->bi_bdev->num_irps_pending);

	if (status != STATUS_SUCCESS) {
		if (status == STATUS_INVALID_DEVICE_REQUEST && stack_location->MajorFunction == IRP_MJ_FLUSH_BUFFERS)
			status = STATUS_SUCCESS;
	}

	if (status != STATUS_SUCCESS) {
		printk(KERN_WARNING "DrbdIoCompletion: I/O failed with error %x\n", Irp->IoStatus.Status);
	}

	if (test_inject_faults(&bio->bi_bdev->inject_on_completion, "assuming completion routine was send an error (enabled for this device)"))
		status = STATUS_IO_DEVICE_ERROR;

	if (test_inject_faults(&inject_on_completion, "assuming completion routine was send an error (enabled for all devices)"))
		status = STATUS_IO_DEVICE_ERROR;

	one_big_request = bio->bi_using_big_buffer;

	if (bio->bi_using_big_buffer) {
		if (stack_location->MajorFunction == IRP_MJ_READ) {
			/* copy data to io_vecs */
			int i;
			int offset;

			offset = 0;
			for (i=0;i<bio->bi_vcnt;i++) {
				RtlCopyMemory(((char*)bio->bi_io_vec[i].bv_page->addr)+bio->bi_io_vec[i].bv_offset, ((char*)bio->bi_big_buffer)+offset, bio->bi_io_vec[i].bv_len);
				offset += bio->bi_io_vec[i].bv_len;
			}
			if (offset != bio->bi_big_buffer_size) {
				printk("Warning: size mismatch in DrbdIoCompletin(): offset is %d bio->bi_big_buffer_size is %d\n", offset, bio->bi_big_buffer_size);
			}
		}
			/* Do not kfree() the big buffer here, since
			 * there is still an IRP referencing it.
			 */
	}

	if (stack_location->MajorFunction == IRP_MJ_READ && bio->bi_iter.bi_sector == 0 && bio->bi_iter.bi_size >= 512 && !bio->dont_patch_boot_sector) {
		if (test_and_set_bit(BI_WINDRBD_FLAG_BOOTSECTOR_PATCHED, &bio->bi_windrbd_flags) == 0) {
			void *buffer = bio->bi_io_vec[0].bv_page->addr;
			patch_boot_sector(buffer, 1, 0);
		}
	}

	int num_completed, device_failed;

	spin_lock_irqsave(&bio->device_failed_lock, flags);
	num_completed = atomic_inc_return(&bio->bi_requests_completed);
	device_failed = bio->device_failed;
	if (status != STATUS_SUCCESS)
		bio->device_failed = 1;
	spin_unlock_irqrestore(&bio->device_failed_lock, flags);

	if (!device_failed && (num_completed == bio->bi_num_requests || status != STATUS_SUCCESS || one_big_request)) {
		bio->bi_status = win_status_to_blk_status(status);
		bio_endio(bio);
	}

	bio_put(bio);

		/* Tell IO manager that it should not touch the
		 * irp. It has yet to be freed together with the
		 * bio.
		 */

	return STATUS_MORE_PROCESSING_REQUIRED;
}

static LONGLONG windrbd_get_volsize(struct block_device *dev)
{
	NTSTATUS status;
	KEVENT event;
	struct _IRP *newIrp;
	struct _IO_STACK_LOCATION *s;
	ULONGLONG ret;

	mutex_lock(&dev->vol_size_mutex);

// mem_printk("memset %p 0 %d\n", &dev->vol_size_length_information, sizeof(dev->vol_size_length_information));
	memset(&dev->vol_size_length_information, 0, sizeof(dev->vol_size_length_information));

	if (KeGetCurrentIrql() >= APC_LEVEL) {
		printk("cannot run IoBuildDeviceIoControlRequest becauseof IRP(%d)\n", KeGetCurrentIrql());
		mutex_unlock(&dev->vol_size_mutex);

		return -1;
	}

	KeInitializeEvent(&event, NotificationEvent, FALSE);
	newIrp = IoBuildDeviceIoControlRequest(IOCTL_DISK_GET_LENGTH_INFO,
       		dev->windows_device, NULL, 0,
		&dev->vol_size_length_information, sizeof(dev->vol_size_length_information), 
		FALSE, &event, &dev->vol_size_io_status);

	if (!newIrp) {
		printk("cannot alloc new IRP\n");
		mutex_unlock(&dev->vol_size_mutex);

		return -1;
	}	
	s = IoGetNextIrpStackLocation(newIrp);

	s->DeviceObject = dev->windows_device;
	s->FileObject = dev->file_object;

	status = IoCallDriver(dev->windows_device, newIrp);

	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, (PLARGE_INTEGER)NULL);
		status = dev->vol_size_io_status.Status;
	}
	if (!NT_SUCCESS(status)) {
	        printk("cannot get volume information, err=0x%x\n", status);
		mutex_unlock(&dev->vol_size_mutex);
		return -1;
	}

	ret = dev->vol_size_length_information.Length.QuadPart;
	mutex_unlock(&dev->vol_size_mutex);

	return ret;
}

static int make_flush_request(struct bio *bio)
{
	NTSTATUS status;
	PIO_STACK_LOCATION next_stack_location;

	bio->bi_irps[bio->bi_this_request] = IoBuildAsynchronousFsdRequest(
				IRP_MJ_FLUSH_BUFFERS,
				bio->bi_bdev->windows_device,
				NULL,
				0,
				NULL,
				&bio->io_stat
				);

	if (bio->bi_irps[bio->bi_this_request] == NULL) {
		printk(KERN_ERR "Cannot build IRP.\n");
		return -EIO;
	}

	IoSetCompletionRoutine(bio->bi_irps[bio->bi_this_request], DrbdIoCompletion, bio, TRUE, TRUE, TRUE);

	next_stack_location = IoGetNextIrpStackLocation (bio->bi_irps[bio->bi_this_request]);

	next_stack_location->DeviceObject = bio->bi_bdev->windows_device;
	next_stack_location->FileObject = bio->bi_bdev->file_object;

	bio_get(bio);	/* To be put in completion routine (bi_endio) */


	atomic_inc(&bio->bi_bdev->num_irps_pending);
	status = IoCallDriver(bio->bi_bdev->windows_device, bio->bi_irps[bio->bi_this_request]);

	if (status != STATUS_SUCCESS && status != STATUS_PENDING) {
		if (status == STATUS_INVALID_DEVICE_REQUEST) {
				/* seems to be the common case, only
				   print for debugging. For Windows7.
				 */
			dbg(KERN_INFO "Flush not supported by windows device, ignored\n");
			return 0;
		}
		printk(KERN_WARNING "flush request failed with status %x\n", status);
		return EIO;	/* Positive value means do not call endio function */
	}

	/* For Server 2016 (probably also Windows 10) kernels this succeeds. */
	dbg("flush succeeded\n");
	return 0;
}

static int windrbd_generic_make_request(struct bio *bio, bool single_request)
{
	NTSTATUS status;

	void *buffer;
	ULONG io = 0;
	PIO_STACK_LOCATION next_stack_location;
	int i;
	int err = -EIO;
	unsigned int the_size;

	if (bio->bi_vcnt == 0) {
		printk(KERN_ERR "Warning: bio->bi_vcnt == 0\n");
		return -EIO;
	}
	if (bio_data_dir(bio) == WRITE) {
		io = IRP_MJ_WRITE;
	} else {
		io = IRP_MJ_READ;
	}

	bio->bi_io_vec[bio->bi_this_request].offset.QuadPart = bio->bi_iter.bi_sector << 9;
	if (single_request) {
		buffer = bio->bi_big_buffer;
		the_size = bio->bi_big_buffer_size;
	} else {
		buffer = (void*) (((char*) bio->bi_io_vec[bio->bi_this_request].bv_page->addr) + bio->bi_io_vec[bio->bi_this_request].bv_offset);
		the_size = bio->bi_io_vec[bio->bi_this_request].bv_len;
	}

	/* Leave that here for now it is sometimes useful: */
// printk("(%s) Local I/O(%s): offset=0x%llx sect=0x%llx total sz=%d IRQL=%d buf=0x%p bi_vcnt: %d bv_offset=%d the_size=%d bio=%p\n", current->comm, (io == IRP_MJ_READ) ? "READ" : "WRITE", bio->bi_io_vec[bio->bi_this_request].offset.QuadPart, bio->bi_io_vec[bio->bi_this_request].offset.QuadPart / 512, bio->bi_iter.bi_size, KeGetCurrentIrql(), buffer, bio->bi_vcnt, bio->bi_io_vec[0].bv_offset, the_size, bio);

/* Make a copy of the (page cache) buffer and write the copy to the
   backing device. Reason is that on write (for example formatting the
   disk) modified buffer gets written to the peer device(s) which in turn
   prevents them to mount the NTFS (or other) file system.
 */


	if (io == IRP_MJ_WRITE && bio->bi_iter.bi_sector == 0 && bio->bi_iter.bi_size >= 512 && bio->bi_this_request == 0 && !bio->dont_patch_boot_sector) {
		bio->patched_bootsector_buffer = kmalloc(the_size, 0, 'DRBD');
		if (bio->patched_bootsector_buffer == NULL)
			return -ENOMEM;

		memcpy(bio->patched_bootsector_buffer, buffer, the_size);
		buffer = bio->patched_bootsector_buffer;

		patch_boot_sector(buffer, 0, 0);
	}

		/* TODO: io_stat not used at all? */
	bio->bi_irps[bio->bi_this_request] = IoBuildAsynchronousFsdRequest(
				io,
				bio->bi_bdev->windows_device,
				buffer,
				the_size,
				&bio->bi_io_vec[bio->bi_this_request].offset,
				&bio->bi_io_vec[bio->bi_this_request].io_stat
				);

	if (!bio->bi_irps[bio->bi_this_request]) {
		printk(KERN_ERR "IoBuildAsynchronousFsdRequest: cannot alloc new IRP for io %d, device %p, buffer %p, the_size %d, offset %lld%\n", io, bio->bi_bdev->windows_device, buffer, the_size, bio->bi_io_vec[bio->bi_this_request].offset.QuadPart);
		return -ENOMEM;
	}
	IoSetCompletionRoutine(bio->bi_irps[bio->bi_this_request], DrbdIoCompletion, bio, TRUE, TRUE, TRUE);

	next_stack_location = IoGetNextIrpStackLocation (bio->bi_irps[bio->bi_this_request]);

	next_stack_location->DeviceObject = bio->bi_bdev->windows_device;
	next_stack_location->FileObject = bio->bi_bdev->file_object;

	if (io == IRP_MJ_WRITE) {
		next_stack_location->Parameters.Write.Length = the_size;
	}
	if (io == IRP_MJ_READ) {
		next_stack_location->Parameters.Read.Length = the_size;
	}
	bio_get(bio);	/* To be put in completion routine (bi_endio) */

	int device_failed = bio->device_failed;

	if (device_failed ||
	    (bio->bi_bdev && bio->bi_bdev->drbd_device &&
	     bio->bi_bdev->drbd_device->disk_state[NOW] <= D_FAILED)) {
		printk("Device already failed, cancelling IRP\n");
		IoCancelIrp(bio->bi_irps[bio->bi_this_request]);
		return EIO;
	}

	if (test_inject_faults(&bio->bi_bdev->inject_on_request, "assuming request failed (enabled for this device)"))
		return -EIO;

	if (test_inject_faults(&inject_on_request, "assuming request failed (enabled for all devices)"))
		return -EIO;

	atomic_inc(&bio->bi_bdev->num_irps_pending);
	part_stat_add(bio->bi_bdev, sectors[io == IRP_MJ_READ ? STAT_READ : STAT_WRITE], the_size / 512);

	status = IoCallDriver(bio->bi_bdev->windows_device, bio->bi_irps[bio->bi_this_request]);

		/* either STATUS_SUCCESS or STATUS_PENDING */
		/* Update: may also return STATUS_ACCESS_DENIED */

	if (status != STATUS_SUCCESS && status != STATUS_PENDING) {
		printk("IoCallDriver status %x, I/O on backing device failed, bio: %p\n", status, bio);
		return EIO; /* positive value indicating that irp is already
			     * completed and bio is already freed (bio_endio
			     * must not be called).
			     */
	}
	return 0;
}

int generic_make_request(struct bio *bio)
{
	int ret;
	sector_t sector;
	sector_t orig_sector;
	int total_size;
	int orig_size;
	int e;
	int flush_request;
	atomic_inc(&bio->bi_bdev->num_bios_pending);

	bio_get(bio);

	flush_request = ((bio->bi_opf & REQ_PREFLUSH) != 0);
	bio->bi_num_requests = bio->bi_vcnt + flush_request;

	if (bio->bi_num_requests == 0) {
		bio->bi_status = 0;
		bio_endio(bio);
		bio_put(bio);
		return 0;
	}

		/* In case we fail early, bi_irps[n].MdlAddress must be
		 * NULL.
		 */
	bio->bi_irps = kzalloc(sizeof(*bio->bi_irps)*bio->bi_num_requests, 0, 'XXXX');
	if (bio->bi_irps == NULL) {
		bio->bi_status = BLK_STS_IOERR;
		bio_endio(bio);
		bio_put(bio);
		return -ENOMEM;
	}
	atomic_set(&bio->bi_requests_completed, 0);

	orig_sector = sector = bio->bi_iter.bi_sector;
	orig_size = bio->bi_iter.bi_size;

	bio->bi_using_big_buffer = false;
	if (bio_data_dir(bio) == READ && bio->bi_vcnt > 1) {
		total_size = 0;
		for (e = 0; e < bio->bi_vcnt; e++)
			total_size += bio->bi_io_vec[e].bv_len;

		if (total_size != bio->bi_iter.bi_size) {
			printk("Warning: size mismatch in generic_make_request(): total_size is %d bi_size is %d\n", total_size, bio->bi_iter.bi_size);
		}
		bio->bi_big_buffer_size = total_size;
		bio->bi_big_buffer = kmalloc(total_size, 0, 'XXXX');

		if (bio->bi_big_buffer != NULL) {
			bio->bi_this_request = 0;
			bio->bi_using_big_buffer = true;

			ret = windrbd_generic_make_request(bio, true);

			if (ret < 0) {
				bio->bi_status = BLK_STS_IOERR;
				bio_endio(bio);
				goto out;
			}
			if (ret > 0)
				goto out;
		}
	}
	ret = 0;

	if (!bio->bi_using_big_buffer) {
		for (bio->bi_this_request=0;
		     bio->bi_this_request<(bio->bi_num_requests - flush_request);
		     bio->bi_this_request++) {
			total_size = bio->bi_io_vec[bio->bi_this_request].bv_len;
			bio->bi_iter.bi_sector = sector;
			bio->bi_iter.bi_size = total_size;

			ret = windrbd_generic_make_request(bio, false);

			if (ret < 0) {
				bio->bi_status = BLK_STS_IOERR;
				bio_endio(bio);
				goto out;
			}
			if (ret > 0)
				goto out;
			sector += total_size >> 9;
		}
	}

	if (flush_request) {
		ret = make_flush_request(bio);

		if (ret < 0) {
			bio->bi_status = BLK_STS_IOERR;
			bio_endio(bio);
		}
	}

out:
	if (ret > 0)
		ret = -ret;

	bio->bi_iter.bi_sector = orig_sector;
	bio->bi_iter.bi_size = orig_size;

	bio_put(bio);

	return ret;
}

void bio_endio(struct bio *bio)
{
	int error = blk_status_to_errno(bio->bi_status);

	bio_get(bio);


// printk("1\n");
	if (bio->bi_end_io != NULL) {
		if (error != 0)
			printk("Warning: thread(%s) bio_endio error with err=%d.\n", current->comm, error);


// printk("into bi_end_io ...\n");
		bio->bi_end_io(bio);
// printk("out of bi_end_io ...\n");
	} else
		printk("Warning: thread(%s) bio(%p) no bi_end_io function.\n", current->comm, bio);

// printk("2\n");
	atomic_dec(&bio->bi_bdev->num_bios_pending);
	if (atomic_read(&bio->bi_bdev->num_bios_pending) == 0) {
// printk("into wake_up %p\n", &bio->bi_bdev->bios_event);
		wake_up(&bio->bi_bdev->bios_event);
	}
// printk("3\n");

	bio_put(bio);
}

void __list_del_entry(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
}

void list_del_init(struct list_head *entry)
{
	__list_del_entry(entry);
	INIT_LIST_HEAD(entry);
}

int hlist_unhashed(const struct hlist_node *h)
{
	return !h->pprev;
}

void __hlist_del(struct hlist_node *n)
{
	struct hlist_node *next = n->next;
	struct hlist_node **pprev = n->pprev;
	*pprev = next;
	if (next)
		next->pprev = pprev;
}

void INIT_HLIST_NODE(struct hlist_node *h)
{
    h->next = NULL;
    h->pprev = NULL;
}

void hlist_del_init(struct hlist_node *n)
{
	if (!hlist_unhashed(n)) {
		__hlist_del(n);
		INIT_HLIST_NODE(n);
	}
}

void hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{
	struct hlist_node *first = h->first;
	n->next = first;
	if (first)
		first->pprev = &n->next;
	h->first = n;
	n->pprev = &h->first;
}


/*----------------------------------------------------------------------*/
/* This was shamelessly stolen from the Linux kernel.			*/
/*----------------------------------------------------------------------*/

static const unsigned int crctab32[] = {
	0x00000000U, 0x77073096U, 0xee0e612cU, 0x990951baU, 0x076dc419U,
	0x706af48fU, 0xe963a535U, 0x9e6495a3U, 0x0edb8832U, 0x79dcb8a4U,
	0xe0d5e91eU, 0x97d2d988U, 0x09b64c2bU, 0x7eb17cbdU, 0xe7b82d07U,
	0x90bf1d91U, 0x1db71064U, 0x6ab020f2U, 0xf3b97148U, 0x84be41deU,
	0x1adad47dU, 0x6ddde4ebU, 0xf4d4b551U, 0x83d385c7U, 0x136c9856U,
	0x646ba8c0U, 0xfd62f97aU, 0x8a65c9ecU, 0x14015c4fU, 0x63066cd9U,
	0xfa0f3d63U, 0x8d080df5U, 0x3b6e20c8U, 0x4c69105eU, 0xd56041e4U,
	0xa2677172U, 0x3c03e4d1U, 0x4b04d447U, 0xd20d85fdU, 0xa50ab56bU,
	0x35b5a8faU, 0x42b2986cU, 0xdbbbc9d6U, 0xacbcf940U, 0x32d86ce3U,
	0x45df5c75U, 0xdcd60dcfU, 0xabd13d59U, 0x26d930acU, 0x51de003aU,
	0xc8d75180U, 0xbfd06116U, 0x21b4f4b5U, 0x56b3c423U, 0xcfba9599U,
	0xb8bda50fU, 0x2802b89eU, 0x5f058808U, 0xc60cd9b2U, 0xb10be924U,
	0x2f6f7c87U, 0x58684c11U, 0xc1611dabU, 0xb6662d3dU, 0x76dc4190U,
	0x01db7106U, 0x98d220bcU, 0xefd5102aU, 0x71b18589U, 0x06b6b51fU,
	0x9fbfe4a5U, 0xe8b8d433U, 0x7807c9a2U, 0x0f00f934U, 0x9609a88eU,
	0xe10e9818U, 0x7f6a0dbbU, 0x086d3d2dU, 0x91646c97U, 0xe6635c01U,
	0x6b6b51f4U, 0x1c6c6162U, 0x856530d8U, 0xf262004eU, 0x6c0695edU,
	0x1b01a57bU, 0x8208f4c1U, 0xf50fc457U, 0x65b0d9c6U, 0x12b7e950U,
	0x8bbeb8eaU, 0xfcb9887cU, 0x62dd1ddfU, 0x15da2d49U, 0x8cd37cf3U,
	0xfbd44c65U, 0x4db26158U, 0x3ab551ceU, 0xa3bc0074U, 0xd4bb30e2U,
	0x4adfa541U, 0x3dd895d7U, 0xa4d1c46dU, 0xd3d6f4fbU, 0x4369e96aU,
	0x346ed9fcU, 0xad678846U, 0xda60b8d0U, 0x44042d73U, 0x33031de5U,
	0xaa0a4c5fU, 0xdd0d7cc9U, 0x5005713cU, 0x270241aaU, 0xbe0b1010U,
	0xc90c2086U, 0x5768b525U, 0x206f85b3U, 0xb966d409U, 0xce61e49fU,
	0x5edef90eU, 0x29d9c998U, 0xb0d09822U, 0xc7d7a8b4U, 0x59b33d17U,
	0x2eb40d81U, 0xb7bd5c3bU, 0xc0ba6cadU, 0xedb88320U, 0x9abfb3b6U,
	0x03b6e20cU, 0x74b1d29aU, 0xead54739U, 0x9dd277afU, 0x04db2615U,
	0x73dc1683U, 0xe3630b12U, 0x94643b84U, 0x0d6d6a3eU, 0x7a6a5aa8U,
	0xe40ecf0bU, 0x9309ff9dU, 0x0a00ae27U, 0x7d079eb1U, 0xf00f9344U,
	0x8708a3d2U, 0x1e01f268U, 0x6906c2feU, 0xf762575dU, 0x806567cbU,
	0x196c3671U, 0x6e6b06e7U, 0xfed41b76U, 0x89d32be0U, 0x10da7a5aU,
	0x67dd4accU, 0xf9b9df6fU, 0x8ebeeff9U, 0x17b7be43U, 0x60b08ed5U,
	0xd6d6a3e8U, 0xa1d1937eU, 0x38d8c2c4U, 0x4fdff252U, 0xd1bb67f1U,
	0xa6bc5767U, 0x3fb506ddU, 0x48b2364bU, 0xd80d2bdaU, 0xaf0a1b4cU,
	0x36034af6U, 0x41047a60U, 0xdf60efc3U, 0xa867df55U, 0x316e8eefU,
	0x4669be79U, 0xcb61b38cU, 0xbc66831aU, 0x256fd2a0U, 0x5268e236U,
	0xcc0c7795U, 0xbb0b4703U, 0x220216b9U, 0x5505262fU, 0xc5ba3bbeU,
	0xb2bd0b28U, 0x2bb45a92U, 0x5cb36a04U, 0xc2d7ffa7U, 0xb5d0cf31U,
	0x2cd99e8bU, 0x5bdeae1dU, 0x9b64c2b0U, 0xec63f226U, 0x756aa39cU,
	0x026d930aU, 0x9c0906a9U, 0xeb0e363fU, 0x72076785U, 0x05005713U,
	0x95bf4a82U, 0xe2b87a14U, 0x7bb12baeU, 0x0cb61b38U, 0x92d28e9bU,
	0xe5d5be0dU, 0x7cdcefb7U, 0x0bdbdf21U, 0x86d3d2d4U, 0xf1d4e242U,
	0x68ddb3f8U, 0x1fda836eU, 0x81be16cdU, 0xf6b9265bU, 0x6fb077e1U,
	0x18b74777U, 0x88085ae6U, 0xff0f6a70U, 0x66063bcaU, 0x11010b5cU,
	0x8f659effU, 0xf862ae69U, 0x616bffd3U, 0x166ccf45U, 0xa00ae278U,
	0xd70dd2eeU, 0x4e048354U, 0x3903b3c2U, 0xa7672661U, 0xd06016f7U,
	0x4969474dU, 0x3e6e77dbU, 0xaed16a4aU, 0xd9d65adcU, 0x40df0b66U,
	0x37d83bf0U, 0xa9bcae53U, 0xdebb9ec5U, 0x47b2cf7fU, 0x30b5ffe9U,
	0xbdbdf21cU, 0xcabac28aU, 0x53b39330U, 0x24b4a3a6U, 0xbad03605U,
	0xcdd70693U, 0x54de5729U, 0x23d967bfU, 0xb3667a2eU, 0xc4614ab8U,
	0x5d681b02U, 0x2a6f2b94U, 0xb40bbe37U, 0xc30c8ea1U, 0x5a05df1bU,
	0x2d02ef8dU
};

static unsigned long partial_crc32_one(unsigned char c, unsigned long crc)
{
	return crctab32[(crc ^ c) & 0xff] ^ (crc >> 8);
}

static unsigned long partial_crc32(const char *s, size_t len, unsigned long crc)
{
	while (len--)
		crc = partial_crc32_one(*s++, crc);
	return crc;
}

unsigned long crc32(const char *s, size_t len)
{
	return partial_crc32(s, len, 0xffffffff) ^ 0xffffffff;
}

static const u32 crc32c_table[256] = { 
	0x00000000L, 0xF26B8303L, 0xE13B70F7L, 0x1350F3F4L,
	0xC79A971FL, 0x35F1141CL, 0x26A1E7E8L, 0xD4CA64EBL,
	0x8AD958CFL, 0x78B2DBCCL, 0x6BE22838L, 0x9989AB3BL,
	0x4D43CFD0L, 0xBF284CD3L, 0xAC78BF27L, 0x5E133C24L,
	0x105EC76FL, 0xE235446CL, 0xF165B798L, 0x030E349BL,
	0xD7C45070L, 0x25AFD373L, 0x36FF2087L, 0xC494A384L,
	0x9A879FA0L, 0x68EC1CA3L, 0x7BBCEF57L, 0x89D76C54L,
	0x5D1D08BFL, 0xAF768BBCL, 0xBC267848L, 0x4E4DFB4BL,
	0x20BD8EDEL, 0xD2D60DDDL, 0xC186FE29L, 0x33ED7D2AL,
	0xE72719C1L, 0x154C9AC2L, 0x061C6936L, 0xF477EA35L,
	0xAA64D611L, 0x580F5512L, 0x4B5FA6E6L, 0xB93425E5L,
	0x6DFE410EL, 0x9F95C20DL, 0x8CC531F9L, 0x7EAEB2FAL,
	0x30E349B1L, 0xC288CAB2L, 0xD1D83946L, 0x23B3BA45L,
	0xF779DEAEL, 0x05125DADL, 0x1642AE59L, 0xE4292D5AL,
	0xBA3A117EL, 0x4851927DL, 0x5B016189L, 0xA96AE28AL,
	0x7DA08661L, 0x8FCB0562L, 0x9C9BF696L, 0x6EF07595L,
	0x417B1DBCL, 0xB3109EBFL, 0xA0406D4BL, 0x522BEE48L,
	0x86E18AA3L, 0x748A09A0L, 0x67DAFA54L, 0x95B17957L,
	0xCBA24573L, 0x39C9C670L, 0x2A993584L, 0xD8F2B687L,
	0x0C38D26CL, 0xFE53516FL, 0xED03A29BL, 0x1F682198L,
	0x5125DAD3L, 0xA34E59D0L, 0xB01EAA24L, 0x42752927L,
	0x96BF4DCCL, 0x64D4CECFL, 0x77843D3BL, 0x85EFBE38L,
	0xDBFC821CL, 0x2997011FL, 0x3AC7F2EBL, 0xC8AC71E8L,
	0x1C661503L, 0xEE0D9600L, 0xFD5D65F4L, 0x0F36E6F7L,
	0x61C69362L, 0x93AD1061L, 0x80FDE395L, 0x72966096L,
	0xA65C047DL, 0x5437877EL, 0x4767748AL, 0xB50CF789L,
	0xEB1FCBADL, 0x197448AEL, 0x0A24BB5AL, 0xF84F3859L,
	0x2C855CB2L, 0xDEEEDFB1L, 0xCDBE2C45L, 0x3FD5AF46L,
	0x7198540DL, 0x83F3D70EL, 0x90A324FAL, 0x62C8A7F9L,
	0xB602C312L, 0x44694011L, 0x5739B3E5L, 0xA55230E6L,
	0xFB410CC2L, 0x092A8FC1L, 0x1A7A7C35L, 0xE811FF36L,
	0x3CDB9BDDL, 0xCEB018DEL, 0xDDE0EB2AL, 0x2F8B6829L,
	0x82F63B78L, 0x709DB87BL, 0x63CD4B8FL, 0x91A6C88CL,
	0x456CAC67L, 0xB7072F64L, 0xA457DC90L, 0x563C5F93L,
	0x082F63B7L, 0xFA44E0B4L, 0xE9141340L, 0x1B7F9043L,
	0xCFB5F4A8L, 0x3DDE77ABL, 0x2E8E845FL, 0xDCE5075CL,
	0x92A8FC17L, 0x60C37F14L, 0x73938CE0L, 0x81F80FE3L,
	0x55326B08L, 0xA759E80BL, 0xB4091BFFL, 0x466298FCL,
	0x1871A4D8L, 0xEA1A27DBL, 0xF94AD42FL, 0x0B21572CL,
	0xDFEB33C7L, 0x2D80B0C4L, 0x3ED04330L, 0xCCBBC033L,
	0xA24BB5A6L, 0x502036A5L, 0x4370C551L, 0xB11B4652L,
	0x65D122B9L, 0x97BAA1BAL, 0x84EA524EL, 0x7681D14DL,
	0x2892ED69L, 0xDAF96E6AL, 0xC9A99D9EL, 0x3BC21E9DL,
	0xEF087A76L, 0x1D63F975L, 0x0E330A81L, 0xFC588982L,
	0xB21572C9L, 0x407EF1CAL, 0x532E023EL, 0xA145813DL,
	0x758FE5D6L, 0x87E466D5L, 0x94B49521L, 0x66DF1622L,
	0x38CC2A06L, 0xCAA7A905L, 0xD9F75AF1L, 0x2B9CD9F2L,
	0xFF56BD19L, 0x0D3D3E1AL, 0x1E6DCDEEL, 0xEC064EEDL,
	0xC38D26C4L, 0x31E6A5C7L, 0x22B65633L, 0xD0DDD530L,
	0x0417B1DBL, 0xF67C32D8L, 0xE52CC12CL, 0x1747422FL,
	0x49547E0BL, 0xBB3FFD08L, 0xA86F0EFCL, 0x5A048DFFL,
	0x8ECEE914L, 0x7CA56A17L, 0x6FF599E3L, 0x9D9E1AE0L,
	0xD3D3E1ABL, 0x21B862A8L, 0x32E8915CL, 0xC083125FL,
	0x144976B4L, 0xE622F5B7L, 0xF5720643L, 0x07198540L,
	0x590AB964L, 0xAB613A67L, 0xB831C993L, 0x4A5A4A90L,
	0x9E902E7BL, 0x6CFBAD78L, 0x7FAB5E8CL, 0x8DC0DD8FL,
	0xE330A81AL, 0x115B2B19L, 0x020BD8EDL, 0xF0605BEEL,
	0x24AA3F05L, 0xD6C1BC06L, 0xC5914FF2L, 0x37FACCF1L,
	0x69E9F0D5L, 0x9B8273D6L, 0x88D28022L, 0x7AB90321L,
	0xAE7367CAL, 0x5C18E4C9L, 0x4F48173DL, 0xBD23943EL,
	0xF36E6F75L, 0x0105EC76L, 0x12551F82L, 0xE03E9C81L,
	0x34F4F86AL, 0xC69F7B69L, 0xD5CF889DL, 0x27A40B9EL,
	0x79B737BAL, 0x8BDCB4B9L, 0x988C474DL, 0x6AE7C44EL,
	0xBE2DA0A5L, 0x4C4623A6L, 0x5F16D052L, 0xAD7D5351L
};

uint32_t crc32c(uint32_t crc, const uint8_t *data, unsigned int length)
{
	while (length--)
		crc = crc32c_table[(crc ^ *data++) & 0xFFL] ^ (crc >> 8);

	return crc;
}

inline void __list_add_rcu(struct list_head *new, struct list_head *prev, struct list_head *next)
{
	new->next = next;
	new->prev = prev;
	rcu_assign_pointer(list_next_rcu(prev), new);
	next->prev = new;
}

void list_del_rcu(struct list_head *entry)
{
     __list_del(entry->prev, entry->next);
     entry->prev = LIST_POISON2;
}

void list_add_rcu(struct list_head *new, struct list_head *head)
{
    __list_add_rcu(new, head, head->next);
}

void list_add_tail_rcu(struct list_head *new, struct list_head *head)
{
     __list_add_rcu(new, head->prev, head);
}

static spinlock_t global_queue_lock;

struct request_queue *blk_alloc_queue(int unused)
{
	struct request_queue *q;

	q = kzalloc(sizeof(struct request_queue), 0, 'E5DW');
	if (q == NULL)
		return NULL;

		/* might be overridden later, see drbd_main.c
		 * It is used only once to set the bit flags.
		 */
	q->queue_lock = &global_queue_lock;

	return q;
}

void blk_cleanup_queue(struct request_queue *q)
{
	kfree(q);
}

struct gendisk *alloc_disk(int minors)
{	
	struct gendisk *p = kzalloc(sizeof(struct gendisk), 0, '44DW');
	return p;
}

struct gendisk *blk_alloc_disk(int unused)
{
	struct request_queue *q;
	struct gendisk *disk;

	q = blk_alloc_queue(unused);
	if (!q)
		return NULL;

	disk = alloc_disk(0);
	if (!disk) {
		blk_cleanup_queue(q);
		return NULL;
	}
	disk->queue = q;
	return disk;
}

void put_disk(struct gendisk *disk)
{
	kfree(disk);
}

void blk_cleanup_disk(struct gendisk *disk)
{
	blk_cleanup_queue(disk->queue);
	put_disk(disk);
}

struct block_device *bdget_disk(struct gendisk *disk, int partno)
{
	if (partno > 0)
		printk("Warning: bdget_disk called with partno = %d, we do not support partitions\n", partno);

	if (disk) {
		if (disk->bdev)
			kref_get(&disk->bdev->kref);
		else
			printk("Warning: disk->bdev is NULL in bdget_disk\n");

		return disk->bdev;
	}
	printk("Warning: disk is NULL in bdget_disk\n");
	return NULL;
}

void blk_queue_make_request(struct request_queue *q, make_request_fn *mfn)
{
	// not support
}

void blk_queue_flush(struct request_queue *q, unsigned int flush)
{
}

/**
 * blk_queue_segment_boundary - set boundary rules for segment merging
 * @q:  the request queue for the device
 * @mask:  the memory boundary mask
 **/
void blk_queue_segment_boundary(struct request_queue *q, unsigned long mask)
{
	if (mask < PAGE_SIZE - 1) {
		mask = PAGE_SIZE - 1;
		printk(KERN_INFO "%s: set to minimum %lx\n",
		       __func__, mask);
	}

	q->limits.seg_boundary_mask = mask;
}

/* Not implemented. */
int blk_stack_limits(struct queue_limits *t, struct queue_limits *b,
                            sector_t offset)
{
	return 0;
}

/* Not implemented. */
void blk_queue_update_readahead(struct request_queue *q)
{
}

struct bio_set *bioset_create(unsigned int pool_size, unsigned int front_pad)
{
	// not support
	return NULL;
}

void bioset_free(struct bio_set *bs)
{
    /* Will never be called, as bioset_create() above isn't implemented. */
    (void)bs;
}


//
// porting netlink interface 
//
unsigned char *skb_put(struct sk_buff *skb, unsigned int len)
{
	unsigned char *tmp = skb_tail_pointer(skb);
	// SKB_LINEAR_ASSERT(skb);
	skb->tail += len;
	skb->len  += len;

	if (skb->tail > skb->end)
	{
		printk("drbd:skb_put: skb_over_panic\n");
	}

	return tmp;
}

void *genlmsg_put(struct sk_buff *skb, u32 pid, u32 seq,
				       struct genl_family *family, int flags, u8 cmd)
{
	struct nlmsghdr *nlh;
	struct genlmsghdr *hdr;

	nlh = nlmsg_put(skb, pid, seq, family->id, GENL_HDRLEN + family->hdrsize, flags);
	if (nlh == NULL)
		return NULL;

	hdr = nlmsg_data(nlh);
	hdr->cmd = cmd;
	hdr->version = family->version;
	hdr->reserved = 0;

	return (char *) hdr + GENL_HDRLEN;
}

void *genlmsg_put_reply(struct sk_buff *skb,
                         struct genl_info *info,
                         struct genl_family *family,
                         int flags, u8 cmd)
{
	return genlmsg_put(skb, info->snd_portid, info->snd_seq, family, flags, cmd);
}

void genlmsg_cancel(struct sk_buff *skb, void *hdr)
{

}

int _DRBD_ratelimit(struct ratelimit_state *rs, const char * func, const char * __FILE, const int __LINE)
{
	int ret;
	
	if (!rs || rs->interval == 0)
		return 1;

	/* TODO: why? */
	if (KeGetCurrentIrql() > DISPATCH_LEVEL)
	{
		return 1;
	}

	/*
	 * If we contend on this state's lock then almost
	 * by definition we are too busy to print a message,
	 * in addition to the one that will be printed by
	 * the entity that is holding the lock already:
	 */
	if (!spin_trylock(&rs->lock))
		return 0;

	if (!rs->begin)
		rs->begin = jiffies;

	if (time_is_before_jiffies(rs->begin + rs->interval)){
		if (rs->missed)
			printk("%s(%s@%d): %d callbacks suppressed\n", func, __FILE, __LINE, rs->missed);
		rs->begin = jiffies;
		rs->printed = 0;
		rs->missed = 0;
	}

	if (rs->burst && rs->burst > rs->printed){
		rs->printed++;
		ret = 1;
	} else {
		rs->missed++;
		ret = 0;
	}
	spin_unlock(&rs->lock);

	return ret;
}

static int idr_max(int layers)
{
	int bits = min_t(int, layers * IDR_BITS, MAX_IDR_SHIFT);
	return (1 << bits) - 1;
}

#define __round_mask(x, y) ((y) - 1)
#define round_up(x, y) ((((x) - 1) | __round_mask(x, y)) + 1)

void *idr_get_next(struct idr *idp, int *nextidp)
{
	struct idr_layer *p, *pa[MAX_IDR_LEVEL + 1];
	struct idr_layer **paa = &pa[0];
	int id = *nextidp;
	int n, max;

	/* find first ent */
	if (!idp)
		return NULL;

	n = idp->layers * IDR_BITS;
	max = 1 << n;
	p = rcu_dereference_raw(idp->top);
	if (!p)
		return NULL;

	while (id < max) {
		while (n > 0 && p) {
			n -= IDR_BITS;
			*paa++ = p;
			p = rcu_dereference_raw(p->ary[(id >> n) & IDR_MASK]);
		}

		if (p) {
			*nextidp = id;
			return p;
		}

		id += 1 << n;
		while (n < fls(id)) {
			n += IDR_BITS;
			p = *--paa;
		}
	}
	return NULL;
}

// DW-1109: delete drbd bdev when ref cnt gets 0, clean up all resources that has been created in create_drbd_block_device.
void delete_block_device(struct kref *kref)
{
	struct block_device *bdev = container_of(kref, struct block_device, kref);

	if (bdev->bdflush_thread != NULL) {
		bdev->bdflush_should_run = 0;
		wake_up(&bdev->bdflush_event);
		wait_for_completion(&bdev->bdflush_terminated);
		bdev->bdflush_thread = NULL;
	}
	if (bdev->bd_disk) {
		if (bdev->bd_disk->queue)
			blk_cleanup_queue(bdev->bd_disk->queue);
		put_disk(bdev->bd_disk);
	}
//	ObDereferenceObject(bdev->file_object);
	kfree(bdev->path_to_device.Buffer);

	list_del(&bdev->backing_devices_list);
	kfree(bdev);
}

static NTSTATUS resolve_nt_kernel_link(UNICODE_STRING *upath, UNICODE_STRING *link_target)
{
	NTSTATUS status;
	OBJECT_ATTRIBUTES device_attributes;
	ULONG link_target_length;
	HANDLE link_handle;

	InitializeObjectAttributes(&device_attributes, upath, OBJ_FORCE_ACCESS_CHECK, NULL, NULL);
	status = ZwOpenSymbolicLinkObject(&link_handle, GENERIC_READ, &device_attributes);
	if (!NT_SUCCESS(status)) {
		printk("ZwOpenSymbolicLinkObject: Cannot open link object, status = %x, path = %S\n", status, upath->Buffer);
		return status;
	}

	status = ZwQuerySymbolicLinkObject(link_handle, link_target, &link_target_length);
	if (!NT_SUCCESS(status)) {
		printk("ZwQuerySymbolicLinkObject: Cannot get link target name, status = %x, path = %S\n", status, upath->Buffer);
		goto out_close_handle;
	}
	if (link_target_length >= link_target->MaximumLength) {
		printk("ZwQuerySymbolicLinkObject: Link target name exceeds %lu bytes (is %lu bytes), path = %S\n", link_target->MaximumLength, link_target_length, upath->Buffer);
		goto out_close_handle;
	}
	link_target->Buffer[link_target_length] = 0;
	printk(KERN_INFO "Symbolic link points to %S\n", link_target->Buffer);

out_close_handle:
	ZwClose(link_handle);
	return status;
}


int resolve_ascii_path(const char *path, UNICODE_STRING *path_to_device)
{
	ANSI_STRING apath;
	UNICODE_STRING link_name;
	NTSTATUS status;
	int ret = 0;

	RtlInitAnsiString(&apath, path);
	status = RtlAnsiStringToUnicodeString(&link_name, &apath, TRUE);
	if (status != STATUS_SUCCESS) {
		printk(KERN_ERR "Cannot allocate unicode device name.\n");
		return -ENOMEM;
	}

	path_to_device->Buffer = kmalloc(sizeof(WCHAR) * 1024, 0, 'BDRX');
	if (path_to_device->Buffer == NULL) {
		printk(KERN_ERR "Cannot allocate device name.\n");

		RtlFreeUnicodeString(&link_name);
		return -ENOMEM;
	}

	path_to_device->MaximumLength = 1024-1;
	path_to_device->Length = 0;

	printk("Link is %S\n", link_name.Buffer);
	if (resolve_nt_kernel_link(&link_name, path_to_device) != STATUS_SUCCESS) {
		printk("Could not resolve link.\n");
		ret = -EINVAL;
	}
	RtlFreeUnicodeString(&link_name);

	return ret;
}

static struct _DEVICE_OBJECT *find_windows_device(UNICODE_STRING *path, struct _FILE_OBJECT ** file_object)
{
	struct _DEVICE_OBJECT *windows_device;
	struct _FILE_OBJECT *FileObject;
	NTSTATUS status;
	int tries = 10;

	do {
		status = IoGetDeviceObjectPointer(path, STANDARD_RIGHTS_ALL | FILE_ALL_ACCESS, &FileObject, &windows_device);
		if (status != STATUS_SHARING_VIOLATION)
			break;
		printk(KERN_WARNING, "Sharing violation on %S somebody is using it right now (tries = %d)?\n", path->Buffer, tries);
		msleep(1000);
	} while (--tries > 0);

	if (!NT_SUCCESS(status))
	{
		printk(KERN_ERR "Cannot get device object for %S status: %x, does it exist?\n", path->Buffer, status);
		return NULL;
	}
	printk("IoGetDeviceObjectPointer %S succeeded, targetdev is %p\n", path->Buffer, windows_device);

	*file_object = FileObject;
	return windows_device;
}

static void backingdev_check_endio(struct bio *bio)
{
	struct completion *c = (struct completion*) bio->bi_private;
	complete(c);
}

static int check_if_backingdev_contains_filesystem(struct block_device *dev)
{
	struct bio *b = bio_alloc(0, 1, 'DRBD');
	int i;
	struct completion c;
	int ret;

	static char boot_sector[8192];
	struct page *p;

	mutex_lock(&read_bootsector_mutex);

/*
	p = kzalloc(sizeof(struct page),0, 'D3DW'); 
	if (!p)	{
		printk(KERN_ERR "alloc_page struct page failed\n");
		mutex_unlock(&read_bootsector_mutex);
		return 1;
	}
	p->addr = boot_sector+(4096-((ULONG_PTR)boot_sector & 4095));
*/
	p = alloc_page(sizeof(struct page)); 
	if (!p)	{
		printk(KERN_ERR "alloc_page struct page failed\n");
		mutex_unlock(&read_bootsector_mutex);
		return 1;
	}

	bio_add_page(b, p, 512, 0);
	bio_set_op_attrs(b, REQ_OP_READ, 0);

	b->bi_end_io = backingdev_check_endio;
	b->dont_patch_boot_sector = true;

	b->bi_iter.bi_sector = 0;
	init_completion(&c);
	b->bi_private = &c;
	bio_set_dev(b, dev);
	b->bi_paged_memory = 1;

	submit_bio(b);
	wait_for_completion(&c);

	ret = is_filesystem(p->addr);

	bio_put(b);
	put_page(p);

	mutex_unlock(&read_bootsector_mutex);

	return ret;
}

/* This creates a new block device associated with the windows
   device pointed to by path.
 */

struct block_device *blkdev_get_by_path(const char *path, fmode_t mode, void *holder)
{
	struct block_device *block_device;
	NTSTATUS status;
	struct _DEVICE_OBJECT *windows_device;
	struct _FILE_OBJECT *file_object;
	int err = 0;
	UNICODE_STRING path_to_device;

	err = resolve_ascii_path(path, &path_to_device);
	if (err < 0)
		return ERR_PTR(err);

	list_for_each_entry(struct block_device, block_device, &backing_devices, backing_devices_list) {
		if (RtlEqualUnicodeString(&block_device->path_to_device, &path_to_device, TRUE)) {
			printk(KERN_DEBUG "Block device for windows device %S already open, reusing it (block_device %p)\n", path_to_device.Buffer, block_device);

			kfree(path_to_device.Buffer);
				/* we got an extra reference in 
				 * find_windows_device()
				 */
			kref_get(&block_device->kref);
			return block_device;
		}
	}

	windows_device = find_windows_device(&path_to_device, &file_object);
	if (windows_device == NULL) {
		err = -ENOENT;
		goto out_no_windows_device;
	}

	block_device = kzalloc(sizeof(struct block_device), 0, 'DBRD');
	if (block_device == NULL) {
		printk("could not allocate block_device.\n");
		err = -ENOMEM;
		goto out_no_block_device;
	}
	block_device->windows_device = windows_device;
	block_device->bd_disk = alloc_disk(0);
	if (!block_device->bd_disk)
	{
		printk("Failed to allocate gendisk NonPagedMemory\n");
		err = -ENOMEM;
		goto out_no_disk;
	}

	block_device->bd_disk->queue = blk_alloc_queue(0);
	if (!block_device->bd_disk->queue)
	{
		printk("Failed to allocate request_queue NonPagedMemory\n");
		err = -ENOMEM;
		goto out_no_queue;
	}
	block_device->bd_disk->part0 = block_device;
	block_device->bd_disk->bdev = block_device;

	IoInitializeRemoveLock(&block_device->remove_lock, 'DRBD', 0, 0);
	status = IoAcquireRemoveLock(&block_device->remove_lock, NULL);
	if (!NT_SUCCESS(status)) {
		printk("Failed to acquire remove lock, status is %s\n", status);
		err = -EBUSY;
		goto out_remove_lock_error;
	}

        kref_init(&block_device->kref);
 
	block_device->bd_contains = block_device;
	block_device->bd_parent = NULL;

		/* TODO: not always? */
	block_device->bd_block_size = 512;
	block_device->bd_disk->queue->logical_block_size = 512;
	block_device->bd_disk->queue->max_hw_sectors = DRBD_MAX_BIO_SIZE >> 9;

	block_device->file_object = file_object;
	block_device->is_backing_device = true;

	mutex_init(&block_device->vol_size_mutex);
	block_device->d_size = windrbd_get_volsize(block_device);
	if (block_device->d_size == -1) {
		printk(KERN_ERR "Cannot get volsize.\n");
		err = -EINVAL;
		goto out_get_volsize_error;
	}
	block_device->path_to_device = path_to_device;

	init_waitqueue_head(&block_device->bios_event);
	atomic_set(&block_device->num_bios_pending, 0);
	atomic_set(&block_device->num_irps_pending, 0);

	INIT_LIST_HEAD(&block_device->write_cache);
	spin_lock_init(&block_device->write_cache_lock);

	inject_faults(-1, &block_device->inject_on_completion);
	inject_faults(-1, &block_device->inject_on_request);

	if (check_if_backingdev_contains_filesystem(block_device)) {
		printk(KERN_ERR "Backing device contains filesystem, refusing to use it.\n");
		printk(KERN_INFO "You may want to do something like windrbd hide-filesystem <drive-letter-of-backing-dev>\n");
		err = -EINVAL;
		goto out_get_volsize_error;
	}

	printk(KERN_DEBUG "blkdev_get_by_path succeeded %p windows_device %p.\n", block_device, block_device->windows_device);

	list_add(&block_device->backing_devices_list, &backing_devices);

/*
printk("freeing file object ...\n");
ObDereferenceObject(file_object);
printk("done\n");
*/

	return block_device;

out_get_volsize_error:
	IoReleaseRemoveLock(&block_device->remove_lock, NULL);
out_remove_lock_error:
	blk_cleanup_queue(block_device->bd_disk->queue);
out_no_queue:
	put_disk(block_device->bd_disk);
out_no_disk:
	kfree(block_device);
out_no_block_device:
	ObDereferenceObject(file_object);
out_no_windows_device:
	kfree(path_to_device.Buffer);

	return ERR_PTR(err);
}

void panic(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	printk(fmt, args);
	va_end(args);
		/* TODO: no */
//	KeBugCheckEx(0xddbd, (ULONG_PTR)__FILE__, (ULONG_PTR)__func__, 0x12345678, 0xd8bdd8bd);
}

int scnprintf(char * buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int i = 0;

	va_start(args, fmt);
	i = _vsnprintf_s(buf, size, _TRUNCATE, fmt, args);
	va_end(args);
	return (-1 == i) ? (size - 1) : i;
}

int vscnprintf(char * buf, size_t size, const char *fmt, va_list args)
{
	int i = 0;

	i = _vsnprintf_s(buf, size, _TRUNCATE, fmt, args);
	return (-1 == i) ? (size - 1) : i;
}

int list_is_singular(const struct list_head *head)
{
	return !list_empty(head) && (head->next == head->prev);
}

void __list_cut_position(struct list_head *list, struct list_head *head, struct list_head *entry)
{
	struct list_head *new_first = entry->next;
	list->next = head->next;
	list->next->prev = list;
	list->prev = entry;
	entry->next = list;
	head->next = new_first;
	new_first->prev = head;
}
// from linux kernel 3.14 
void list_cut_position(struct list_head *list, struct list_head *head, struct list_head *entry)
{
	if (list_empty(head))
		return;
	if (list_is_singular(head) &&
		(head->next != entry && head != entry))
		return;
	if (entry == head)
		INIT_LIST_HEAD(list);
	else
		__list_cut_position(list, head, entry);
}

struct blk_plug_cb *blk_check_plugged(blk_plug_cb_fn unplug, void *data,
				      int size)
{
	return NULL;
}

sector_t windrbd_get_capacity(struct block_device *bdev)
{
	long long d_size;

	if (bdev == NULL) {
		printk(KERN_WARNING "Warning: bdev is NULL in windrbd_get_capacity\n");
		return 0;
	}

	if (bdev->is_backing_device) {
		d_size = windrbd_get_volsize(bdev);
		if (d_size == -1)
			printk(KERN_WARNING "Warning: could not get size of backing device\n");
		else {
			if (bdev->d_size != d_size) {
				if (windrbd_rescan_bus() < 0)
					printk(KERN_WARNING "Warning: Size changed but couldn't rescan WinDRBD bus device\n");
				printk(KERN_INFO "Block device size changed from %lld to %lld\n", bdev->d_size, d_size);
				bdev->d_size = d_size;
			}
		}
	}

	return bdev->d_size >> 9;
}

sector_t get_capacity(struct gendisk *disk)
{
	if (disk->bdev != NULL)
		return windrbd_get_capacity(disk->bdev);

	printk("Warning: get_capacity without block device called.\n");
	return 0;
}

/* Space is allocated by this function and must be freed by the
   caller.
 */

static int minor_to_windows_device_name(UNICODE_STRING *name, int minor, int dos_device)
{
	NTSTATUS status;
	size_t len = 32;

	name->Buffer = kmalloc(len * sizeof(name->Buffer[0]), GFP_KERNEL, 'DRBD');

	if (name->Buffer == NULL) {
		printk("couldn't allocate memory for name buffer\n");
		return -ENOMEM;
	}
	name->Length = 0;
	name->MaximumLength = (len - 1) * sizeof(name->Buffer[0]);

	if (dos_device)
		status = RtlUnicodeStringPrintf(name, L"\\DosDevices\\Drbd%d", minor);
	else
		status = RtlUnicodeStringPrintf(name, L"\\Device\\Drbd%d", minor);

	if (status != STATUS_SUCCESS) {
		printk("minor_to_dos_name: couldn't printf device name for minor %d status: %x\n", minor, status);

		kfree(name->Buffer);
		return -EINVAL;
	}
	name->Buffer[name->Length / sizeof(name->Buffer[0])] = 0;

	return 0;
}

static int create_dos_link(struct block_device *dev)
{
	NTSTATUS status;
	UNICODE_STRING dos_name;

	if (minor_to_windows_device_name(&dos_name, dev->drbd_device->minor, 1) < 0) {
		printk("Warning: could not create DOS filename\n");
		return -1;
	}

	status = IoCreateSymbolicLink(&dos_name, &dev->path_to_device);
	if (status != STATUS_SUCCESS) {
		printk("windrbd_mount: couldn't symlink %S to %S status: %x\n", dev->path_to_device.Buffer, dos_name.Buffer, status);
		kfree(dos_name.Buffer);
		return -1;
	}
	printk("Created symlink from %S to %S\n", dos_name.Buffer, dev->path_to_device.Buffer);
	kfree(dos_name.Buffer);

	return 0;
}

static int remove_dos_link(struct block_device *dev)
{
	NTSTATUS status;
	UNICODE_STRING dos_name;

	if (minor_to_windows_device_name(&dos_name, dev->drbd_device->minor, 1) < 0) {
		printk("Warning: could not create DOS filename\n");
		return -1;
	}

	status = IoDeleteSymbolicLink(&dos_name);
	if (status != STATUS_SUCCESS) {
		printk("windrbd_mount: couldn't remove symlink %S status: %x\n", dos_name.Buffer, status);
		kfree(dos_name.Buffer);
		return -1;
	}
	printk("Removed symlink from %S to %S\n", dos_name.Buffer, dev->path_to_device.Buffer);
	kfree(dos_name.Buffer);

	return 0;
}


int windrbd_create_windows_device(struct block_device *bdev)
{
        PDEVICE_OBJECT new_device;
	struct block_device_reference *bdev_ref;
	NTSTATUS status;
	DEVICE_TYPE device_type;

	if (bdev->windows_device != NULL)
		printk(KERN_WARNING "Warning: block device %p already has a windows device (%p)\n", bdev, bdev->windows_device);

	KeClearEvent(&bdev->device_started_event);
	KeClearEvent(&bdev->device_ejected_event);
	KeClearEvent(&bdev->bus_device_iterated);
	bdev->ejected = false;

		/* By default, this creates an object accessible only
		 * by the Administrator user from user space. If this
		 * does not work one day, use IoCreateDeviceSecure with
		 * SDDL_DEVOBJ_SYS_ALL_ADM_ALL as the sddl parameter.
		 */

	// device_type = (bdev->is_disk_device ? FILE_DEVICE_DISK : FILE_DEVICE_UNKNOWN);
	device_type = FILE_DEVICE_DISK;

	status = IoCreateDevice(mvolDriverObject, 
		                sizeof(struct block_device_reference), 
		                &bdev->path_to_device,
		                device_type,
                                FILE_DEVICE_SECURE_OPEN,
                                FALSE,
                                &new_device);

	if (status != STATUS_SUCCESS) {
		printk("Couldn't create new block device %S for minor %d status: %x\n", bdev->path_to_device.Buffer, bdev->minor, status);

		return -1;
	}
	printk("Windows device %p created for block device %p\n", new_device, bdev);
	bdev->windows_device = new_device;
		/* It might still be set from a former drbdadm secondary.
		 */
	bdev->delete_pending = false;
	bdev->about_to_delete = false;
	KeClearEvent(&bdev->device_removed_event);

	bdev_ref = new_device->DeviceExtension;
	bdev_ref->bdev = bdev;
	bdev_ref->magic = BLOCK_DEVICE_UPPER_MAGIC;
	IoInitializeRemoveLock(&bdev_ref->w_remove_lock, 'DRBD', 0, 0);
	status = IoAcquireRemoveLock(&bdev_ref->w_remove_lock, NULL);
	if (!NT_SUCCESS(status)) {
		printk("Failed to acquire remove lock, status is %s\n", status);
		return -1;
	}
	bdev->ref = bdev_ref;

		/* TODO: makes a difference? */
		/* TODO: also try DO_BUFFERED_IO */
	new_device->Flags |= DO_DIRECT_IO;
	new_device->Flags &= ~DO_DEVICE_INITIALIZING;

	create_dos_link(bdev);

	return 0;
}

static void windrbd_remove_windows_device(struct block_device *bdev)
{
	struct _DEVICE_OBJECT *windows_device;
	struct block_device_reference *ref;

// printk("Start removing device %S\n", bdev->path_to_device.Buffer);

	if (bdev->windows_device == NULL) {
		printk(KERN_WARNING "Windows device does not exist in block device %p.\n", bdev);
		return;
	}

		/* Thereby, the windows device will not be reported
		 * again when rescanning the bus and will be deleted
		 * by sending a PnP REMOVE_DEVICE request.
		 */

	bdev->delete_pending = true;

		/* counterpart to acquiring in bdget() */
	IoReleaseRemoveLock(&bdev->ref->w_remove_lock, NULL);

	remove_dos_link(bdev);

	LARGE_INTEGER timeout;
	NTSTATUS status;
	dbg("Requesting eject of Windows device minor %d\n", bdev->drbd_device->minor);
	IoRequestDeviceEject(bdev->windows_device);
	dbg("Eject returned minor %d\n", bdev->drbd_device->minor);

	timeout.QuadPart = -10*1000*1000*10; /* 10 seconds */
	status = KeWaitForSingleObject(&bdev->device_ejected_event, Executive, KernelMode, FALSE, &timeout);
	if (status == STATUS_TIMEOUT)
		printk("Warning: no eject event after 10 seconds, giving up.\n");

	dbg("Device ejected minor %d\n", bdev->drbd_device->minor);
	if (windrbd_rescan_bus() < 0) {
		/* TODO: check if there are still references (PENDING_DELETE) */

		printk("PnP did not work, removing device manually.\n");
		IoDeleteDevice(bdev->windows_device);
	} else {
// printk("waiting for device being removed via IRP_MN_REMOVE_DEVICE minor %d bdev is %p windows device is %p\n", bdev->drbd_device->minor, bdev, bdev->windows_device);
		timeout.QuadPart = -100*1000*1000*10; /* 100 seconds, sometimes it really takes very long, no idea why */
//		status = KeWaitForSingleObject(&bdev->device_removed_event, Executive, KernelMode, FALSE, &timeout);

		/* We have to wait for REMOVE_DEVICE .. there could be a
	         * BSOD if we didn't (when the DRBD device is brought down,
                 * and up again and then a REMOVE_DEVICE for the old device
                 * comes. So no timeout here. Sorry but drbdadm secondary
                 * takes about 40 seconds now, but there is no BSOD.
                 */
		status = KeWaitForSingleObject(&bdev->device_removed_event, Executive, KernelMode, FALSE, NULL);
/*
		if (status == STATUS_TIMEOUT)
			printk("Warning: no IRP_MN_REMOVE_DEVICE received after 100 seconds, giving up.\n");
*/

// printk("finished. minor %d now waiting for bus device to report device as missing bdev is %p windows device is %p\n", bdev->drbd_device->minor, bdev, bdev->windows_device);

		timeout.QuadPart = -10*1000*1000*10; /* 10 seconds */
		status = KeWaitForSingleObject(&bdev->bus_device_iterated, Executive, KernelMode, FALSE, &timeout);
		if (status == STATUS_TIMEOUT)
			printk("Warning: no reiteration of bus device after 10 seconds, giving up.\n");
// printk("Excellent we are almost gone .. now really deleting the device ... bdev is %p windows device is %p\n", bdev, bdev->windows_device);
		IoDeleteDevice(bdev->windows_device);
		dbg("Done.\n");
	}
	bdev->windows_device = NULL;
}

/* This is DRBD specific: DRBD calls this only once (same for
 * bdput(). Really we should have a list of known upper block_devices
 * and return an existing for the minor to properly mimic Linux'
 * behaviour.
 */

struct block_device *bdget(dev_t device_no)
{
	dev_t minor = MINOR(device_no);
	NTSTATUS status;
	struct block_device *block_device;
	int ret;

	block_device = kzalloc(sizeof(struct block_device), 0, 'DRBD');
	if (block_device == NULL)
		return NULL;

	if (minor_to_windows_device_name(&block_device->path_to_device, minor, 0) < 0)
		goto out_path_to_device_failed;

	kref_init(&block_device->kref);

	block_device->minor = minor;
	block_device->bd_block_size = 512;
	block_device->mount_point.Buffer = NULL;

/* TODO: to test 'auto-promote' */
// block_device->is_bootdevice = 1;
block_device->my_auto_promote = 1;
/* TODO: test shifting .. and appending sectors */
/* Now read the boot sector from DRBD and decide about partition table then */
// block_device->data_shift = 128;
// block_device->appended_sectors = 128;
// block_device->data_shift = 0x22;
// block_device->appended_sectors = 0x22;
printk("Test: testing partition table injection (%lld sectors shift).\n", block_device->data_shift);
		/* Currently all devices are disk devices, that
		 * is they are managed by plug and play manager.
		 * Set this flag early, else Windows will not
		 * find the disk device.
		 */
	block_device->is_disk_device = true;

	inject_faults(-1, &block_device->inject_on_completion);
	inject_faults(-1, &block_device->inject_on_request);

	KeInitializeEvent(&block_device->primary_event, NotificationEvent, FALSE);
	KeInitializeEvent(&block_device->capacity_event, NotificationEvent, FALSE);
	KeInitializeEvent(&block_device->device_removed_event, NotificationEvent, FALSE);
	KeInitializeEvent(&block_device->device_started_event, NotificationEvent, FALSE);
	KeInitializeEvent(&block_device->device_ejected_event, NotificationEvent, FALSE);
	KeInitializeEvent(&block_device->bus_device_iterated, NotificationEvent, FALSE);
	spin_lock_init(&block_device->complete_request_spinlock);

	printk(KERN_INFO "Created new block device %S (minor %d).\n", block_device->path_to_device.Buffer, minor);
	
	return block_device;
/*
create_windows_device_failed:
	IoReleaseRemoveLock(&block_device->remove_lock, NULL);
*/
/*
out_remove_lock_failed:
	kfree(block_device->path_to_device.Buffer);
*/
out_path_to_device_failed:
	kfree(block_device);

	return NULL;
}

	/* This function is roughly taken from:
	 * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/mountmgr/ni-mountmgr-ioctl_mountmgr_create_point
	 */

/* TODO: mount point (drive letter) must always be upper case */

/* TODO: this code (using mount manager) really does not have any
 * advantages and is quite complicated (it works, but ...), so
 * maybe we switch back to just creating the symbolic link.
 */

/* TODO: oops fsutil reports and error when just creating the
 * symlink, good that we still have this code:
 */

static int mountmgr_create_point(struct block_device *dev)
{
	struct _MOUNTMGR_CREATE_POINT_INPUT *create_point;
	size_t create_point_size = sizeof(MOUNTMGR_CREATE_POINT_INPUT) +
                      dev->mount_point.Length + dev->path_to_device.Length;
	UNICODE_STRING mountmgr_name;
	struct _FILE_OBJECT *mountmgr_file_object;
	struct _DEVICE_OBJECT *mountmgr_device_object;
	KEVENT event;
	struct _IO_STATUS_BLOCK *io_status;
	NTSTATUS status;
	struct _IRP *irp;
	struct _IO_STACK_LOCATION *s;

	create_point = kzalloc(create_point_size, 0, 'DRBD');
	if (create_point == NULL)
		return -1;

	io_status = kzalloc(sizeof(*io_status), 0, 'DRBD');
	if (io_status == NULL) {
		kfree(create_point);
		return -1;
	}
	create_point->SymbolicLinkNameOffset = sizeof(*create_point);
	create_point->SymbolicLinkNameLength = dev->mount_point.Length;
	create_point->DeviceNameOffset = create_point->SymbolicLinkNameOffset+create_point->SymbolicLinkNameLength;
	create_point->DeviceNameLength = dev->path_to_device.Length;

	RtlCopyMemory(((char*)create_point)+create_point->SymbolicLinkNameOffset, dev->mount_point.Buffer, dev->mount_point.Length);
	RtlCopyMemory(((char*)create_point)+create_point->DeviceNameOffset, dev->path_to_device.Buffer, dev->path_to_device.Length);

	/* Use the name of the mount manager device object
	 * defined in mountmgr.h (MOUNTMGR_DEVICE_NAME) to
	 * obtain a pointer to the mount manager.
	 */

	RtlInitUnicodeString(&mountmgr_name, MOUNTMGR_DEVICE_NAME);
	status = IoGetDeviceObjectPointer(&mountmgr_name, FILE_READ_ATTRIBUTES, &mountmgr_file_object, &mountmgr_device_object);
	if (!NT_SUCCESS(status)) {
		printk(KERN_WARNING "IoGetDeviceObjectPointer %s returned %x\n", MOUNTMGR_DEVICE_NAME, status);
		kfree(create_point);
		kfree(io_status);
		return -1;
	}
	KeInitializeEvent(&event, NotificationEvent, FALSE);
	irp = IoBuildDeviceIoControlRequest(
            IOCTL_MOUNTMGR_CREATE_POINT,
            mountmgr_device_object, create_point, create_point_size,
            NULL, 0, FALSE, &event, io_status);

	if (irp == NULL) {
		printk(KERN_WARNING "Cannot create IRP.\n");
		kfree(create_point);
		kfree(io_status);
		return -1;
	}
        s = IoGetNextIrpStackLocation(irp);

        s->DeviceObject = mountmgr_device_object;
        s->FileObject = mountmgr_file_object;

	/* Send the irp to the mount manager requesting
	 * that a new mount point (persistent symbolic link)
	 * be created for the indicated volume.
	 */

	status = IoCallDriver(mountmgr_device_object, irp);

	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, (PLARGE_INTEGER)NULL);
		status = io_status->Status;
	}

	if (!NT_SUCCESS(status)) {
		printk(KERN_ERR "Registering mount point failed status = %x\n", status);
		kfree(create_point);
		kfree(io_status);
		return -1;
	}
	kfree(create_point);
	kfree(io_status);
	return 0;
}

NTSTATUS pnp_callback(void *notification, void *context)
{
	printk("notification: %p context: %p\n", notification, context);

	return STATUS_SUCCESS;
}

bool windrbd_has_mount_point(struct block_device *dev)
{
	if (dev == NULL)
		return false;

	if (dev->mount_point.Buffer == NULL)
		return false;

	if (dev->mount_point.Buffer[0] == L'\0')
		return false;

	return true;
}

int windrbd_set_mount_point_utf16(struct block_device *dev, const wchar_t *mount_point)
{
	if (mount_point == NULL)
		return -EINVAL;

	if (dev->mount_point.Buffer != NULL) {
		printk("set_mount_point called while there is a mount point registered.\n");

		kfree(dev->mount_point.Buffer);
		dev->mount_point.Buffer = NULL;
	}
		/* empty string means do not mount minor */
	if (mount_point[0] == L'\0')
		return 0;

		/* TODO: later, check if it is a drive letter of the form
		 * [A-Z]: and if not, try to mount it to NTFS directory.
		 */

#define DOS_DEVICES L"\\DosDevices\\"
	size_t len = wcslen(mount_point)+wcslen(DOS_DEVICES)+1;
	size_t size_in_bytes = len * sizeof(wchar_t);
	int dos_devices_len = wcslen(DOS_DEVICES);

	dev->mount_point.Buffer = kmalloc(size_in_bytes, GFP_KERNEL, 'DRBD');
	if (dev->mount_point.Buffer == NULL)
		return -ENOMEM;
	dev->mount_point.Length = size_in_bytes-sizeof(wchar_t);
	dev->mount_point.MaximumLength = size_in_bytes;

	wcscpy(dev->mount_point.Buffer, DOS_DEVICES);
	wcscpy(dev->mount_point.Buffer+dos_devices_len, mount_point);
#undef DOS_DEVICES

	return 0;
}


/* TODO: IMHO this is dead code. Boot devices get their mount points
 * from the Windows kernel (partition manager or something like that).
 */

#if 0

static int create_windows_device_and_mount_it(struct block_device *block_device)
{
	int ret;

	ret = windrbd_create_windows_device(block_device);
	if (ret != 0) {
		printk("Warning: Couldn't create windows device for volume\n");
		return ret;
	}

	ret = windrbd_mount(block_device);
	if (ret != 0)
		printk("Warning: Couldn't mount volume, perhaps the drive letter (%S) is in use?\n", block_device->mount_point.Buffer);


	return ret;
}

#endif

int windrbd_set_mount_point_for_minor_utf16(int minor, const wchar_t *mount_point)
{
	struct drbd_device *drbd_device;
	struct block_device *block_device;
	int ret;

	drbd_device = minor_to_device(minor);
	if (drbd_device == NULL)
		return -ENOENT;		/* no such minor */

	block_device = drbd_device->this_bdev;
	if (block_device == NULL)
		return -ENOENT;

	if (block_device->is_mounted) {
		printk("Attempt to change mount point while mounted. Please do a drbdadm secondary first.\n");
		return -EBUSY;
	}

	ret = windrbd_set_mount_point_utf16(block_device, mount_point);
	if (ret == 0)
		printk("Mount point for minor %d set to %S\n", minor, block_device->mount_point.Buffer);
	else {
		printk("Warning: could not set mount point, error is %d\n", ret);
		return ret;
	}
/*
	if (block_device->is_bootdevice)
		ret = create_windows_device_and_mount_it(block_device);
*/

	return ret;
}

static int windrbd_allocate_io_workqueue(struct block_device *bdev)
{
	bdev->io_workqueue = alloc_ordered_workqueue("windrbd_io", 0);

	if (bdev->io_workqueue == NULL)
		return -ENOMEM;

	return 0;
}

static void windrbd_destroy_io_workqueue(struct block_device *bdev)
{
	if (bdev->io_workqueue != NULL) {
		flush_workqueue(bdev->io_workqueue);
		destroy_workqueue(bdev->io_workqueue);
		bdev->io_workqueue = NULL;
	} else {
		printk("Warning windrbd_destroy_io_workqueue called without workqueue being allocated.\n");
	}
}

/* This is intended to be used by boot code where there are
 * no WinDRBD managed mount points and the device just needs
 * to be created early so that Windows has a boot device.
 */

int windrbd_create_windows_device_for_minor(int minor)
{
	struct drbd_device *drbd_device;
	struct block_device *block_device;
	int ret;

	drbd_device = minor_to_device(minor);
	if (drbd_device == NULL)
		return -ENOENT;		/* no such minor */

	block_device = drbd_device->this_bdev;
	if (block_device == NULL)
		return -ENOENT;

	if (windrbd_allocate_io_workqueue(block_device) < 0) {
		printk("Warning: could not allocate I/O workqueues, I/O might not work.\n");
	}

	ret = windrbd_create_windows_device(block_device);
	if (ret != 0) {
		printk("Warning: Couldn't create windows device for volume\n");
		return ret;
	}
	return ret;
}

int windrbd_mount(struct block_device *dev)
{
	NTSTATUS status;
	UNICODE_STRING vol, partition, hddir, arcname;
	HANDLE h;
	OBJECT_ATTRIBUTES attr;

	if (dev->mount_point.Buffer == NULL) {
		dbg("No mount point set for minor %d, will not be mounted.\n", dev->drbd_device->minor);
		return 0;	/* this is legal */
	}

/*
	status = IoCreateSymbolicLink(&dev->mount_point, &dev->path_to_device);
	if (status != STATUS_SUCCESS) {
		printk("windrbd_mount: couldn't symlink %S to %S status: %x\n", dev->path_to_device.Buffer, dev->mount_point.Buffer, status);
		return -1;

	}
*/

	if (mountmgr_create_point(dev) < 0)
		return -1;

	dev->is_mounted = true;

	printk(KERN_INFO "Assigned device %S the mount point %S\n", dev->path_to_device.Buffer, dev->mount_point.Buffer);

	return 0;
}

int windrbd_umount(struct block_device *bdev)
{
	UNICODE_STRING drive;
	OBJECT_ATTRIBUTES attr;
	HANDLE f;
	IO_STATUS_BLOCK iostat;
	NTSTATUS status;

	if (bdev->mount_point.Buffer == NULL) {
		dbg("windrbd_umount() called without a known mount_point.\n");
		return 0;
	}
// printk("mount point is \"%S\"\n", bdev->mount_point.Buffer);
	if (!bdev->is_mounted) {
		printk("windrbd_umount() called while not mounted.\n");
		return 0;
	}
	InitializeObjectAttributes(&attr, &bdev->mount_point, OBJ_KERNEL_HANDLE, NULL, NULL);

		/* If we are in drbd_create_device() failure path, do
		 * not open the DRBD device, it is already freed.
		 */

	status = ZwOpenFile(&f, GENERIC_READ, &attr, &iostat, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
	if (status != STATUS_SUCCESS) {
		printk("ZwOpenFile failed, status is %x\n", status);
		return -1;
	}

	dbg("About to IoDeleteSymbolicLink(%S)\n", bdev->mount_point.Buffer);
	status = IoDeleteSymbolicLink(&bdev->mount_point);
	if (status != STATUS_SUCCESS) {
		printk("Warning: Failed to remove symbolic link (drive letter) %S, status is %x\n", bdev->mount_point.Buffer, status);
	}

	status = ZwFsControlFile(f, NULL, NULL, NULL, &iostat, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0);

	if (status != STATUS_SUCCESS) {
		printk("ZwFsControlFile failed, status is %x\n", status);
		ZwClose(f);
		return -1;
	}
	ZwClose(f);

	bdev->is_mounted = false;
	return 0;
}

extern int windrbd_check_for_filesystem_and_maybe_start_faking_partition_table(struct block_device *bdev);

int windrbd_become_primary(struct drbd_device *device, const char **err_str)
{
	if (!device->this_bdev->is_bootdevice) {
		if (windrbd_allocate_io_workqueue(device->this_bdev) < 0) {
			printk("Warning: could not allocate I/O workqueues, I/O might not work.\n");
		}
		if (windrbd_check_for_filesystem_and_maybe_start_faking_partition_table(device->this_bdev) < 0) {
			printk("Warning: could not determine if there is a file system on the DRBD device.\n");
		}
		if (windrbd_create_windows_device(device->this_bdev) != 0)
			windrbd_device_error(device, err_str, "Warning: Couldn't create windows device for volume %d\n", device->vnr);

		if (windrbd_mount(device->this_bdev) != 0)
			windrbd_device_error(device, err_str, "Warning: Couldn't mount volume %d, perhaps the drive letter (%S) is in use?\n", device->vnr, device->this_bdev->mount_point.Buffer);

		if (windrbd_rescan_bus() < 0) {
			printk("Warning: could not rescan bus, is the WinDRBD virtual bus device existing?\n");
		}
			/* A PnP disk device. Wait for PnP manager to 
			 * properly start the device else races may happen
			 * (drbdadm secondary might BSOD).
			 */
		KeWaitForSingleObject(&device->this_bdev->device_started_event, Executive, KernelMode, FALSE, NULL);
	}
	KeSetEvent(&device->this_bdev->primary_event, 0, FALSE);

	return 0;
}

int windrbd_become_secondary(struct drbd_device *device, const char **err_str)
{
	if (!device->this_bdev->is_bootdevice) {
		if (windrbd_umount(device->this_bdev) != 0)
			windrbd_device_error(device, err_str, "Warning: couldn't umount volume %d\n", device->vnr);
		windrbd_remove_windows_device(device->this_bdev);

		if (windrbd_rescan_bus() < 0) {
			printk("Warning: could not rescan bus, is the WinDRBD virtual bus device existing?\n");
		}
		windrbd_destroy_io_workqueue(device->this_bdev);
	}

	KeClearEvent(&device->this_bdev->primary_event);

	if (device->open_rw_cnt > 0 || device->open_ro_cnt > 0)
		printk("Forcing close of DRBD device: device->open_rw_cnt is %d, device->open_ro_cnt is %d\n", device->open_rw_cnt, device->open_ro_cnt);

	device->open_rw_cnt = 0;
	device->open_ro_cnt = 0;

	return 0;
}


static void windrbd_destroy_block_device(struct kref *kref)
{
	struct block_device *bdev = container_of(kref, struct block_device, kref);

		/* This is legal. Users may create DRBD devices without
		 * mount point.
		 */
	if (bdev->mount_point.Buffer != NULL) {
		if (bdev->is_mounted)
			windrbd_umount(bdev);

	}
	if (bdev->windows_device != NULL) {
		windrbd_remove_windows_device(bdev);
		windrbd_destroy_io_workqueue(bdev);
	}

		/* Do this after removing device, so that we
		 * we know if it was mounted (non-PnP) or not
		 * (PnP way via REMOVE_DEVICE request).
		 */

	if (bdev->mount_point.Buffer != NULL) {
		kfree(bdev->mount_point.Buffer);
		bdev->mount_point.Buffer = NULL;
	}
	kfree(bdev->path_to_device.Buffer);
	bdev->path_to_device.Buffer = NULL;

	if (bdev->bd_disk != NULL)
		bdev->bd_disk->bdev = NULL;

	if (bdev->disk_prolog != NULL) {
		kfree(bdev->disk_prolog);
		bdev->disk_prolog = NULL;
	}
	if (bdev->disk_epilog != NULL) {
		kfree(bdev->disk_epilog);
		bdev->disk_epilog = NULL;
	}

	kfree(bdev);
		/* Do not set windows device object->DeviceExtension->ref
		 * to NULL here. The object already has been deleted
		 * here.
		 */
}

/* TODO: those 2 function go away */
void windrbd_bdget(struct block_device *this_bdev)
{
	kref_get(&this_bdev->kref);
}

void windrbd_bdput(struct block_device *this_bdev)
{
	kref_put(&this_bdev->kref, windrbd_destroy_block_device);
}

/* See the comment at bdget().
 *
 * DRBD now calls this at least at 2 places (starting from DRBD 9.0.28)
 * so we must ensure that we get the kref thing right (see bdget_disk())
 */

void bdput(struct block_device *this_bdev)
{
	KeSetEvent(&this_bdev->capacity_event, 0, FALSE);
	KeSetEvent(&this_bdev->primary_event, 0, FALSE);

	windrbd_bdput(this_bdev);
}


ktime_t ktime_get(void)
{
	return (ktime_t) { .tv64 = jiffies * (1000*1000*1000/HZ) };
}

ktime_t ktime_get_real(void)
{
	LARGE_INTEGER time;

        KeQuerySystemTime(&time);
		/* ktime is 1 ns, KeQuerySystemTime() is 100 ns */
		/* TODO: but KeQuerySystemTime() is since January 1, 1601 ... */
	return (ktime_t) { .tv64 = time.QuadPart * 100 };
}

int register_blkdev(int major, const char *name)
{
	/* does nothing */
	return 0;
}

void unregister_blkdev(int major, const char *name)
{
	/* does nothing */
}

/* To be implemented. See also
 *   https://msdn.microsoft.com/de-de/library/ff552562(v=vs.85).aspx
 *   https://msdn.microsoft.com/de-de/library/hh439649(v=vs.85).aspx
*/
int blkdev_issue_discard(struct block_device *bdev, sector_t sector,
        sector_t nr_sects, gfp_t gfp_mask, ULONG_PTR flags)
{
	printk("Warning: blkdev_issue_discard not implemented.\n");
	return -EIO;
}

int blkdev_issue_write_same(struct block_device *bdev, sector_t sector,
				sector_t nr_sects, gfp_t gfp_mask,
				struct page *page)
{
	printk("Warning: blkdev_issue_write_same not implemented.\n");
	return -EIO;
}

int kobject_uevent(struct kobject *kobj, enum kobject_action action)
{
	printk("Warning: kobject_uevent not implemented\n");

	return 0;
}

static spinlock_t cpu_cache_spinlock;

/* This takes a spinlock and releases it right after, this should
 * make sure that CPU caches are in sync on SMP machines.
 */

void flush_all_cpu_caches(void)
{
	KIRQL flags;

        spin_lock_irqsave(&cpu_cache_spinlock, flags);
        spin_unlock_irqrestore(&cpu_cache_spinlock, flags);
}

	/* NO printk's in here. */
void init_windrbd(void)
{
	mutex_init(&read_bootsector_mutex);
	spin_lock_init(&g_test_and_change_bit_lock);
	spin_lock_init(&cpu_cache_spinlock);
	spin_lock_init(&global_queue_lock);

#ifdef SPIN_LOCK_DEBUG
	KeInitializeSpinLock(&spinlock_lock);
#endif
}

