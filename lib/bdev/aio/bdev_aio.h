/*-
 *   BSD LICENSE
 *
 *   Copyright (c) Intel Corporation.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SPDK_BDEV_AIO_H
#define SPDK_BDEV_AIO_H

#include "spdk/stdinc.h"

#include <libaio.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include "spdk/queue.h"
#include "spdk/bdev.h"

#include "spdk/bdev_module.h"

struct bdev_aio_task {
	struct iocb			iocb;
	uint64_t			len;
	TAILQ_ENTRY(bdev_aio_task)	link;
};

struct bdev_aio_io_channel {
	io_context_t				io_ctx;
	uint64_t				io_inflight;
    // zhou:
	struct spdk_io_channel			*group_ch;
	TAILQ_ENTRY(bdev_aio_io_channel)	link;
	int					efd;
};

typedef void (*spdk_delete_aio_complete)(void *cb_arg, int bdeverrno);

// zhou: AIO bdev private data
struct file_disk {
	struct bdev_aio_task	*reset_task;
	struct spdk_poller	*reset_retry_timer;
    // zhou: each kind of backend storage should include such object.
	struct spdk_bdev	disk;
	char			*filename;
	int			fd;
	TAILQ_ENTRY(file_disk)  link;
	bool			block_size_override;
	spdk_delete_aio_complete	delete_cb_fn;
	void				*delete_cb_arg;
};

struct spdk_bdev *create_aio_disk(const char *name, const char *filename, uint32_t block_size);

void delete_aio_disk(struct spdk_bdev *bdev, spdk_delete_aio_complete cb_fn, void *cb_arg);

#endif /* SPDK_BDEV_AIO_H */
