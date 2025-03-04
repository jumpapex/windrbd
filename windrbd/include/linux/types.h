﻿/*
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

#ifndef _WINDOWS_TYPES_H
#define _WINDOWS_TYPES_H

/* Enable all warnings throws lots of those warnings: */
#pragma warning(disable: 4061 4062 4255 4388 4668 4820 5032  4711 5045)

#include <wdm.h>
#include <ctype.h>
//#include <stdbool.h>
typedef enum __Bool__ {
	false = 0,
	true = 1
} bool;

typedef signed char		    __s8;
typedef unsigned char		__u8;
typedef signed short		__s16;
typedef unsigned short		__u16;
typedef signed int		    __s32;
typedef unsigned int		__u32;
typedef signed long long	__s64;
typedef unsigned long long	__u64;
typedef signed char		    s8;
typedef unsigned char		u8;
typedef signed short		s16;
typedef unsigned short		u16;
typedef signed int		    s32;
typedef unsigned int		u32;
typedef signed long long	s64;
typedef unsigned long long	u64;
typedef unsigned long long	sector_t;

typedef __u16		__le16;
typedef __u16		__be16;
typedef __u32		__le32;
typedef __u32		__be32;
typedef __u64		__le64;
typedef __u64		__be64;


typedef		__u8		u_int8_t;
typedef		__s8		int8_t;
typedef		__u16		u_int16_t;
typedef		__s16		int16_t;
typedef		__u32		u_int32_t;
typedef		__s32		int32_t;

typedef		__u8		uint8_t;
typedef		__u16		uint16_t;
typedef		__u32		uint32_t;

typedef unsigned int gfp_t;

typedef		__u64		uint64_t;
typedef		__u64		u_int64_t;
typedef		__s64		int64_t;

typedef u32 dev_t;

struct callback_head {
	struct callback_head *next;
	void (*func)(struct callback_head *head);
};
#define rcu_head callback_head

typedef void (*rcu_callback_t)(struct rcu_head *head);
typedef void (*call_rcu_func_t)(struct rcu_head *head, rcu_callback_t func);

#ifndef __packed
#define __packed
#endif

typedef LONG_PTR ssize_t;

#endif
