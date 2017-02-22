/*
 * Copyright (c) 2015, Xilinx Inc. and Contributors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Xilinx nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __METAL_COMPAT_H__
#define __METAL_COMPAT_H__
#include "virtio_ring.h"

typedef enum memory_order {
	memory_order_relaxed = __ATOMIC_RELAXED,
	memory_order_consume = __ATOMIC_CONSUME,
	memory_order_acquire = __ATOMIC_ACQUIRE,
	memory_order_release = __ATOMIC_RELEASE,
	memory_order_acq_rel = __ATOMIC_ACQ_REL,
	memory_order_seq_cst = __ATOMIC_SEQ_CST,
} memory_order;

#define atomic_thread_fence __atomic_thread_fence

struct metal_io_region;

typedef unsigned long metal_phys_addr_t;

/** Generic I/O operations. */
struct metal_io_ops {
	uint64_t	(*read)(struct metal_io_region *io,
				unsigned long offset,
				memory_order order,
				int width);
	void		(*write)(struct metal_io_region *io,
				 unsigned long offset,
				 uint64_t value,
				 memory_order order,
				 int width);
	void		(*close)(struct metal_io_region *io);
};

/** Libmetal I/O region structure. */
struct metal_io_region {
	void			*virt;
	const metal_phys_addr_t	*physmap;
	size_t			size;
	unsigned long		page_shift;
	metal_phys_addr_t	page_mask;
	unsigned int		mem_flags;
	struct metal_io_ops	ops;
};

struct metal_sg {
	void *virt; /**< CPU virtual address */
	struct metal_io_region *io; /**< IO region */
	int len; /**< length */
};

#define metal_allocate_memory malloc
#define metal_free_memory free
#define openamp_print printf


/** Bad offset into shared memory or I/O region. */
#define METAL_BAD_OFFSET	((unsigned long)-1)

/** Bad physical address value. */
#define METAL_BAD_PHYS		((metal_phys_addr_t)-1)

/**
 * @brief	Get size of I/O region.
 *
 * @param[in]	io	I/O region handle.
 * @return	Size of I/O region.
 */
static inline size_t metal_io_region_size(struct metal_io_region *io)
{
	return io->size;
}

/**
 * @brief	Get virtual address for a given offset into the I/O region.
 * @param[in]	io	I/O region handle.
 * @param[in]	offset	Offset into shared memory segment.
 * @return	NULL if offset is out of range, or pointer to offset.
 */
static inline void *
metal_io_virt(struct metal_io_region *io, unsigned long offset)
{
#ifdef METAL_INVALID_IO_VADDR
	return (io->virt != METAL_INVALID_IO_VADDR && offset <= io->size
#else
	return (offset <= io->size
#endif
		? (uint8_t *)io->virt + offset
		: NULL);
}

/**
 * @brief	Convert a virtual address to offset within I/O region.
 * @param[in]	io	I/O region handle.
 * @param[in]	virt	Virtual address within segment.
 * @return	METAL_BAD_OFFSET if out of range, or offset.
 */
static inline unsigned long
metal_io_virt_to_offset(struct metal_io_region *io, void *virt)
{
	size_t offset = (uint8_t *)virt - (uint8_t *)io->virt;
	return (offset < io->size ? offset : METAL_BAD_OFFSET);
}

/**
 * @brief	Get physical address for a given offset into the I/O region.
 * @param[in]	io	I/O region handle.
 * @param[in]	offset	Offset into shared memory segment.
 * @return	METAL_BAD_PHYS if offset is out of range, or physical address
 *		of offset.
 */
static inline metal_phys_addr_t
metal_io_phys(struct metal_io_region *io, unsigned long offset)
{
	unsigned long page = offset >> io->page_shift;
	return (io->physmap != NULL && offset <= io->size
		&& io->physmap[page] != METAL_BAD_PHYS
		? io->physmap[page] + (offset & io->page_mask)
		: METAL_BAD_PHYS);
}

/**
 * @brief	Convert a physical address to offset within I/O region.
 * @param[in]	io	I/O region handle.
 * @param[in]	phys	Physical address within segment.
 * @return	METAL_BAD_OFFSET if out of range, or offset.
 */
static inline unsigned long
metal_io_phys_to_offset(struct metal_io_region *io, metal_phys_addr_t phys)
{
	unsigned long offset =
		(io->page_mask == (metal_phys_addr_t)(-1) ?
		phys - io->physmap[0] :  phys & io->page_mask);
	do {
		if (metal_io_phys(io, offset) == phys)
			return offset;
		offset += io->page_mask + 1;
	} while (offset < io->size);
	return METAL_BAD_OFFSET;
}

/**
 * @brief	Convert a physical address to virtual address.
 * @param[in]	io	Shared memory segment handle.
 * @param[in]	phys	Physical address within segment.
 * @return	NULL if out of range, or corresponding virtual address.
 */
static inline void *
metal_io_phys_to_virt(struct metal_io_region *io, metal_phys_addr_t phys)
{
	return metal_io_virt(io, metal_io_phys_to_offset(io, phys));
}

/**
 * @brief	Convert a virtual address to physical address.
 * @param[in]	io	Shared memory segment handle.
 * @param[in]	virt	Virtual address within segment.
 * @return	METAL_BAD_PHYS if out of range, or corresponding
 *		physical address.
 */
static inline metal_phys_addr_t
metal_io_virt_to_phys(struct metal_io_region *io, void *virt)
{
	return metal_io_phys(io, metal_io_virt_to_offset(io, virt));
}

#endif /* __METAL_COMPAT_H__ */
