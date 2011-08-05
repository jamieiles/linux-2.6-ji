/*
 * $picoChipHeaderSubst$
 *
 * Copyright 2010 picoChip Designs LTD, All Rights Reserved.
 * http://www.picochip.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This driver implements a mechanism for sending PDU's for Over The Air
 * (OTA) ciphering then DMA into the PHY running on the picoArray. This
 * uses 1 copy from userspace into a kmalloc()'d buffer then ciphering
 * in place and DMA into the picoArray.
 *
 * A few notes on cache coherency and DMA
 * --------------------------------------
 *
 * Many of the messages that this driver must deal with consist of
 * multiple sub messages that need ciphering and have lengths and
 * offsets that aren't compatible with the requirements of the DMA API.
 * On the specific SOC (ARM1176JZ-s) that this was designed for, this
 * has been shown to be ok but may not function correctly on other
 * architectures. In particular, this assumes that there is no
 * aggressive speculative D$ prefetching and that the mapping is
 * configured as no-write-on-allocate.
 *
 * When we handle a message, we do two things, cipher the sub messages
 * then DMA the whole lot to the picoArray. The ciphering happens in
 * place and will use a DMA_BIDIRECTIONAL mapping and the picoArray DMA
 * will use DMA_TO_DEVICE. When starting a new message, the buffer is
 * aligned to the L1 cache size, sub messages are copied in on whatever
 * alignment it happens to be. Once the end of the message is received,
 * the crypto operations are dispatched. As the DMA API does a
 * clean+invalidate, the crypto engines will see correct data, but
 * software must not touch *any* of the message until all crypto
 * operations have completed. Once complete, the whole message can be
 * DMA mapped for the device and sent by picoIf.
 */
#define DEBUG
#define pr_fmt(fmt)	"pktman: " fmt

#include <asm/atomic.h>

#include <linux/cdev.h>
#include <linux/configfs.h>
#include <linux/crypto.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <linux/picochip/devices/pc3xx.h>
#include <linux/picochip/transports/directdma.h>

#include <linux/packetmanager.h>

#define PKTMAN_MAX_DEVICES	8
#define PKTMAN_MAJOR		190
#define PKTMAN_BUF_SZ		65536
#define PKTMAN_PKT_PADDING	L1_CACHE_BYTES
#define PKTMAN_MAX_CIPHER_LEN	(PAGE_SIZE * 2)

static struct {
	dev_t			    devno;
	struct class	    	    *class;
	struct list_head    	    devices;
	spinlock_t	    	    lock;
	unsigned long		    present_map[BITS_TO_LONGS(PKTMAN_MAX_DEVICES)];
} pktman = {
	.devno		    	    = MKDEV(PKTMAN_MAJOR, 0),
	.lock		    	    = __SPIN_LOCK_UNLOCKED(pktman_lock),
	.devices	    	    = LIST_HEAD_INIT(pktman.devices),
};

/*
 * struct pktman_buf - the fifo for packets before DMA.
 * @buf:	the buffer to write packets from userspace into, which must be
 *		DMA capable.
 * @buf_sz:	the size of the buffer in bytes.
 * @wptr:	the next byte to commence writing packets into from userspace.
 * @rptr:	the next byte where DMA to the picoArray will commence from.
 *
 * The packet manager copies data from userspace into the buffer and updates
 * wptr to point to the next destination. The ciphering operation is queued
 * and once complete the DMA is commenced. At DMA completion, rptr is advanced
 * to the end of the packet.
 */
struct pktman_buf {
	char			    *buf;
	size_t		    	    buf_sz;
	unsigned int	    	    wptr;
	unsigned int	    	    rptr;
};

/*
 * struct pktman_key_ctx - the context for a single ciphering key
 * @cipher:	the crypto API context.
 * @use_count:	the number of in-flight packets using this context.
 * @waitq:	wait queue for key-changes.
 *
 * We maintain a use count and wait queue so that we don't change the key for
 * a context that is being used in a queued crypto operation. If we don't do
 * this then we could change the key after submission but before processing.
 *
 * When changing the key, the user changing the key should wait for the
 * use_count to reach 0 and then change it. When new packets are enqueued the
 * use_count is incremented. As we hold the pktman_dev mutex when changing
 * the key, we don't need any locking and can sleep with the mutex held to
 * prevent any extra packets being added. Once we are changing the key, the
 * use count can only decrease.
 */
struct pktman_key_ctx {
	struct crypto_ablkcipher    *cipher;
	atomic_t		    use_count;
	wait_queue_head_t	    waitq;
};

/*
 * A message that is being built up for ciphering and DMA. A message consists
 * of 0 or more struct pktman_kreq's that form the ciphered sub-messages and a
 * number of plaintext bytes. DMA to the picoArray is started once all of the
 * kreq's have completed.
 */
struct pktman_msg {
	int			    status;	/*
						 * Msg status. DMA can start
						 * once this is 0.
						 */
	size_t			    msg_size;	/*
						 * The number of bytes that
						 * can be DMA'd. This is set
						 * to the number of plaintext
						 * bytes then added to once
						 * each crypto operation
						 * completes.
						 */
	struct list_head	    head;	/*
						 * The position in the
						 * in_progress list for the
						 * pktman_dev.
						 */
	struct list_head	    children;	/*
						 * The struct pktman_kreq's
						 * for the ciphered data.
						 */
};

struct pktman_kreq {
	struct pktman_key_ctx	    *ctx;   /*
					     * The key context used for
					     * ciphering.
					     */
	struct pktman_msg	    *msg;   /*
					     * The message that the request
					     * belongs to.
					     */
	struct pktman_dev	    *pman;  /*
					     * The packet manager handling
					     * this request.
					     */
	int			    status;
	struct list_head	    head;   /*
					     * The position in the in_progress
					     * list.
					     */
	struct list_head	    children;/*
					      * Child nodes. Once they have
					      * completed, we can do the DMA.
					      */
	struct scatterlist	    sg[2];  /*
					     * Where the plaintext is in the
					     * FIFO.
					     */
	char			    iv[8];  /*
					     * The IV to use for ciphering.
					     */

	/*
	 * When we allocate the request we also allocate the crypto
	 * ablkcipher_request. We can't make this a proper member of this
	 * struct as we don't know how big it is.
	 *
	 * struct ablkcipher_request   *areq;
	 */
};

/*
 * The maximum number of requests that may be present in a single iovec. We
 * copy the requests from userspace into a bounce buffer and process them all
 * in one go. This makes it possible to check we have sufficient space in the
 * FIFO for the whole message.
 */
#define PKTMAN_MAX_REQS_PER_IOVEC   512

/*
 * struct pktman_dev - a packet manager instance.
 *
 * @state:	the current transfer state - only one transfer is supported at
 *		a time.
 * @mutex:	mutex for userspace reader/writer acccess.
 * @buf:	the buffer for crypto/DMA operations.
 * @key_ctxs:	the array of contexts for userspace to use for crypto
 *		operations. Setting a key for a nonexistant context will
 *		create the context.
 * @in_progress:the requests that are in progress. Once we need to start
 *	        transferring to the picoArray, we transfer from the head of
 *	        this list until we reach the end or a request that is busy
 *	        ciphering.
 * @picoif:	the picoif context used for the direct dma transport.
 * @xfer_sg:	the scatterlist for the current transfer to the picoArray.
 * @xfer_sg_count: the number of entries used in the scatterlist. 1/2.
 */
struct pktman_dev {
	enum {
		PKTMAN_DEV_STATE_IDLE,
		PKTMAN_DEV_STATE_TRANSFERRING,
		PKTMAN_DEV_STATE_STOPPING,
	}			    state;
	struct device	    	    dev;
	spinlock_t		    lock;
	struct mutex		    mutex;
	struct pktman_buf   	    buf;
	wait_queue_head_t   	    waitq;
	struct config_item	    item;
	struct pktman_key_ctx	    **key_ctxs;
	unsigned int		    nr_key_ctxs;
	struct cdev	    	    cdev;
	struct list_head    	    head;
	int		    	    id;
	unsigned		    dma_channel;
	dev_t		    	    devno;
	struct list_head	    in_progress;
	struct picoif_context	    *picoif;
	struct scatterlist	    xfer_sg[(PKTMAN_BUF_SZ / PAGE_SIZE) + 2];
	int			    xfer_sg_count;
	int			    nr_ciphering;
	atomic_t		    use_count;
	struct pktman_req	    *req_bounce_buf;
	size_t			    poll_wr_thresh;
	struct work_struct	    work;
};

/*
 * pktman_buf_init - initialize a packet manager buffer ready for data.
 * @buf:	the buffer to initialize.
 *
 * The buffer created will be capable of DMA but will not be DMA coherent. The
 * packet manager is responsible for maintaining cache coherency.
 */
static inline int
pktman_buf_init(struct pktman_buf *buf)
{
	buf->wptr	    = 0;
	buf->rptr	    = 0;
	buf->buf_sz	    = PKTMAN_BUF_SZ;
	buf->buf	    = kmalloc(buf->buf_sz, GFP_DMA);

	return buf->buf ? 0 : -ENOMEM;
}

static inline size_t
pktman_buf_len(const struct pktman_buf *buf)
{
	unsigned int wptr = buf->wptr;

	smp_mb();

	return wptr - buf->rptr;
}

static inline size_t
pktman_buf_space(const struct pktman_buf *buf)
{
	return buf->buf_sz - pktman_buf_len(buf);
}

static inline unsigned int
pktman_buf_offset(const struct pktman_buf *buf,
		  unsigned int idx)
{
	return idx & (buf->buf_sz - 1);
}

/*
 * Copy some data from a user buffer into the packet manager FIFO. The FIFO
 * *must* have sufficient space to copy all of the data - we don't do partial
 * writes as we won't be able to do the ciphering.
 *
 * The scatterlist is updated to point to the data once inside the FIFO so
 * that we can pass it to the crypto layer if we need ciphering.
 *
 * Note: the semaphore in the pktman_dev will be held when we add data so we
 * don't need any finer grained locking as this is the only writer of the
 * FIFO.
 */
static ssize_t
pktman_buf_add(struct pktman_buf *buf,
	       const char __user *ubuf,
	       size_t count,
	       unsigned *start_idx,
	       int pad)
{
	size_t l1 = min(count, buf->buf_sz - pktman_buf_offset(buf, buf->wptr));
	size_t padding = 0;
	unsigned oldwptr;

	/* Copy the first amount from the write pointer onwards. */
	if (copy_from_user(buf->buf + pktman_buf_offset(buf, buf->wptr),
			   ubuf, l1))
		return -EFAULT;

	if (copy_from_user(buf->buf, ubuf + l1, count - l1))
		return -EFAULT;

	if (start_idx)
		*start_idx = pktman_buf_offset(buf, buf->wptr);
	buf->wptr += count;

	/*
	 * Pad from the end of the packet to the next L1 cache boundary with
	 * nul bytes so that we don't get any DMA corruption. The PHY must not
	 * interpret the nul bytes as data.
	 */
	if (pad) {
		oldwptr = buf->wptr;
		buf->wptr = roundup(buf->wptr, PKTMAN_PKT_PADDING);
		padding = buf->wptr - oldwptr;
		memset(buf->buf + pktman_buf_offset(buf, oldwptr), 0, padding);
	}

	return count + padding;
}

static struct pktman_msg *
pktman_msg_alloc(struct pktman_dev *pman)
{
	struct pktman_msg *msg = kmalloc(sizeof(*msg), GFP_KERNEL);

	if (msg) {
		msg->status = -EINPROGRESS;
		INIT_LIST_HEAD(&msg->children);
	}

	return msg;
}

/*
 * Allocate a new request structure. We have a minimum amount of context we
 * need to track for all requests, but for cryptwrite requests we also need
 * context for the crypto API request and the IV. Rather than making several
 * calls to kmalloc(), use one call and pad the size so that we allocate all
 * in one go.
 */
static struct pktman_kreq *
pktman_kreq_alloc(struct crypto_ablkcipher *cip)
{
	size_t len = sizeof(struct pktman_kreq);

	if (cip) {
		len += crypto_ablkcipher_alignmask(cip) &
			~(crypto_tfm_ctx_alignment() - 1);
		len = ALIGN(len, crypto_tfm_ctx_alignment());
		len += sizeof(struct ablkcipher_request) +
			crypto_ablkcipher_reqsize(cip);
	}

	return kmalloc(len, GFP_KERNEL);
}

static inline struct ablkcipher_request *
pktman_kreq_ablkreq(struct pktman_kreq *kreq)
{
	struct ablkcipher_request *ablk_req =
		(void *)PTR_ALIGN((char *)kreq + sizeof(*kreq),
				  crypto_tfm_ctx_alignment());

	return ablk_req;
}

static struct pktman_dev *
pktman_find(int id)
{
	struct pktman_dev *pman = NULL;

	list_for_each_entry(pman, &pktman.devices, head)
		if (pman->id == id)
			return pman;

	return NULL;
}

static inline struct pktman_dev *
to_pktman(struct device *dev)
{
	return dev ? container_of(dev, struct pktman_dev, dev) : NULL;
}

static void
pktman_transfer_complete(size_t nbytes,
			 void *cookie);

static int
pktman_open(struct inode *inode,
	    struct file *filp)
{
	int err = -ENODEV, id = iminor(inode);
	struct pktman_dev *pman;

	if (id >= PKTMAN_MAX_DEVICES)
		return -ENODEV;

	pman = pktman_find(id);
	if (!pman)
		return -ENODEV;

	if (mutex_lock_interruptible(&pman->mutex))
		return -ERESTARTSYS;

	if (pman->state == PKTMAN_DEV_STATE_STOPPING) {
		err = -EBUSY;
		goto out;
	}

	filp->private_data = pman;

	/*
	 * If we are the first user of this instance then we need to do the
	 * picoIf open. At this point, the picoArray should be loaded and
	 * running so we can open the DMA.
	 */
	if (1 == atomic_inc_return(&pman->use_count)) {
		pman->picoif = picoif_directdma_open(0,
				PC3XX_DMA_AXI2PICO_0 + pman->dma_channel,
				pktman_transfer_complete);

		if (!pman->picoif || IS_ERR(pman->picoif)) {
			pr_warning("failed to create picoif context\n");
			err = IS_ERR(pman->picoif) ? PTR_ERR(pman->picoif) :
				-ENODEV;
			atomic_dec(&pman->use_count);
			pman->picoif = NULL;
			goto out;
		}
	}

	config_item_get(&pman->item);

	err = 0;
	goto out;

out:
	mutex_unlock(&pman->mutex);

	return err;
}

static int
pktman_release(struct inode *inode,
	       struct file *filp)
{
	struct pktman_dev *pman = filp->private_data;

	if (mutex_lock_interruptible(&pman->mutex))
		return -ERESTARTSYS;

	if (0 == atomic_dec_return(&pman->use_count) && pman->picoif) {
		spin_lock_bh(&pman->lock);
		if (pman->nr_ciphering)
			pman->state = PKTMAN_DEV_STATE_STOPPING;
		else
			schedule_work(&pman->work);
		spin_unlock_bh(&pman->lock);

		picoif_directdma_close(pman->picoif);
		pman->picoif = NULL;
		pman->buf.wptr = pman->buf.rptr = 0;
	}

	mutex_unlock(&pman->mutex);

	return 0;
}

static struct pktman_key_ctx *
pktman_new_key_ctx(const char __user *key,
		   size_t key_len)
{
	struct pktman_key_ctx *ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	int err;

	if (!ctx)
		return ERR_PTR(-ENOMEM);

	init_waitqueue_head(&ctx->waitq);
	atomic_set(&ctx->use_count, 0);

	ctx->cipher = crypto_alloc_ablkcipher("f8(kasumi)", 0, 0);
	if (IS_ERR(ctx->cipher)) {
		err = PTR_ERR(ctx->cipher);
		goto err_cipher;
	}

	goto out;

err_cipher:
	kfree(ctx);
	ctx = ERR_PTR(err);
out:
	return ctx;
}

static int
pktman_resize_ctxs(struct pktman_dev *pman,
		   int count)
{
	struct pktman_key_ctx **old, **new_ctxs =
		kzalloc(count * sizeof(new_ctxs), GFP_KERNEL);

	if (!new_ctxs)
		return -ENOMEM;

	/*
	 * We don't use a krealloc() as we don't want to drop the
	 * existing contexts if we fail.
	 */
	spin_lock_bh(&pktman.lock);
	memcpy(new_ctxs, pman->key_ctxs,
			pman->nr_key_ctxs * sizeof(new_ctxs));
	old = pman->key_ctxs;
	pman->key_ctxs = new_ctxs;
	pman->nr_key_ctxs = count;
	spin_unlock_bh(&pktman.lock);

	kfree(old);

	return 0;
}

static void
pktman_free_all_ctxs(struct pktman_dev *pman)
{
	int i;

	for (i = 0; i < pman->nr_key_ctxs; ++i) {
		if (pman->key_ctxs[i]) {
			crypto_free_ablkcipher(pman->key_ctxs[i]->cipher);
			kfree(pman->key_ctxs[i]);
			pman->key_ctxs[i] = NULL;
		}
	}
}

static int
pktman_setkey(struct pktman_dev *pman,
	      const struct pktman_req *req,
	      struct scatterlist *key_sg)
{
	int err = -ENOMEM;
	struct pktman_key_ctx *ctx;
	char *key;

	/* If we need to resize the key context array then do that here. */
	if (req->ctx >= pman->nr_key_ctxs) {
		err = pktman_resize_ctxs(pman, req->ctx + 1);
		if (err)
			goto out;
	}

	if (!pman->key_ctxs[req->ctx]) {
		ctx = pktman_new_key_ctx(sg_virt(key_sg), req->len);
		if (IS_ERR(ctx)) {
			err = PTR_ERR(ctx);
			goto out;
		}
		pman->key_ctxs[req->ctx] = ctx;
	} else {
		ctx = pman->key_ctxs[req->ctx];
	}

	key = kmalloc(req->len, GFP_KERNEL);
	if (!key)
		goto out;

	if (req->len != sg_copy_to_buffer(key_sg, 1, key, req->len)) {
		err = -EFAULT;
		goto err_copy;
	}

	/*
	 * Now we need to wait for any users of the context to stop using it.
	 */
	if (wait_event_interruptible(ctx->waitq,
				     !atomic_read(&ctx->use_count)))
		err = -ERESTARTSYS;
	else {
		err = crypto_ablkcipher_setkey(ctx->cipher, key, req->len);
	}

err_copy:
	kfree(key);
out:
	return err;
}

/*
 * Construct a scatterlist of data to transfer and start the DMA to the
 * picoArray.
 */
static int
pktman_transfer(struct pktman_dev *pman,
		size_t nbytes)
{
	unsigned nsgs = 0, rptr = pktman_buf_offset(&pman->buf, pman->buf.rptr);
	size_t bytes_added = 0;

	sg_init_table(pman->xfer_sg, (nbytes / PAGE_SIZE) + 2);

	while (bytes_added < nbytes) {
		unsigned page_offs = ((unsigned long)pman->buf.buf + rptr) &
			(PAGE_SIZE - 1);
		size_t len = min(nbytes - bytes_added,
				(size_t)PAGE_SIZE - page_offs);

		sg_set_buf(&pman->xfer_sg[nsgs++], pman->buf.buf + rptr, len);

		rptr += len;
		if (rptr >= pman->buf.buf_sz)
			rptr -= pman->buf.buf_sz;

		bytes_added += len;
	}

	sg_mark_end(&pman->xfer_sg[nsgs - 1]);

	pman->xfer_sg_count = dma_map_sg(&pman->dev, pman->xfer_sg, nsgs,
					 DMA_TO_DEVICE);

	return picoif_directdma_writesg(pman->picoif, pman->xfer_sg,
				        pman->xfer_sg_count, pman);
}

static int
pktman_push(struct pktman_dev *pman);

static void
pktman_transfer_complete(size_t nbytes,
			 void *cookie)
{
	struct pktman_dev *pman = cookie;

	dma_unmap_sg(&pman->dev, pman->xfer_sg, pman->xfer_sg_count,
		     DMA_TO_DEVICE);

	spin_lock(&pman->lock);
	pman->buf.rptr += nbytes;
	if (pman->state != PKTMAN_DEV_STATE_STOPPING)
		pman->state = PKTMAN_DEV_STATE_IDLE;
	if (pktman_push(pman))
		pr_warning("failed to push buffer\n");
	spin_unlock(&pman->lock);

	wake_up_interruptible(&pman->waitq);
}

/*
 * See if there's any data that we can push into the picoArray. We need to
 * start from the beginning of the in_progress list and look for requests with
 * status 0. We build up a total of how many bytes we can transfer up until
 * the end of the FIFO and start a DMA. If a DMA is already in progress then
 * we don't do anything.
 */
static int
pktman_push(struct pktman_dev *pman)
{
	int err = 0;
	size_t nbytes = 0;
	struct pktman_msg *msg, *tmp;

	if (PKTMAN_DEV_STATE_IDLE != pman->state)
		goto out;

	list_for_each_entry_safe(msg, tmp, &pman->in_progress, head) {
		if (0 != msg->status)
			break;

		nbytes += msg->msg_size;
		list_del(&msg->head);
		kfree(msg);
	}

	if (0 == nbytes)
		goto out;

	pman->state = PKTMAN_DEV_STATE_TRANSFERRING;
	err = pktman_transfer(pman, nbytes);
out:

	return err;
}

static void
__pktman_crypt_complete(struct crypto_async_request *req,
			int err)
{
	struct ablkcipher_request *ablk_req = ablkcipher_request_cast(req);
	struct pktman_kreq *kreq = req->data;
	struct pktman_msg *msg = kreq->msg;
	struct pktman_dev *pman = kreq->pman;

	if (unlikely(err)) {
		/*
		 * The crypto operation has failed. There's not a lot we can
		 * do now other than log a failure and clear the ciphertext so
		 * that the picoArray application doesn't see garbage.
		 */
		struct scatterlist *sg;
		int i;

		pr_warning("encryption failed with code %d", err);
		for_each_sg(ablk_req->dst, sg, 2, i) {
			if (!sg)
				break;
			memset(sg_virt(sg), 0, sg->length);
		}
	}

	--kreq->pman->nr_ciphering;
	list_del(&kreq->head);
	if (0 == atomic_sub_return(1, &kreq->ctx->use_count))
		wake_up_interruptible(&kreq->ctx->waitq);
	kfree(kreq);

	/*
	 * If the message has completed then we can try	to send some more to
	 * the picoArray. If it hasn't finished then it's not worth doing
	 * anything as messages complete in order.
	 */
	if (list_empty(&msg->children)) {
		msg->status = 0;
		/*
		 * See if we can start to send some more data to the
		 * picoArray. If a transfer is already in progress we won't do
		 * anything.
		 */
		if (pktman_push(pman))
			pr_warning("failed to push buffer\n");
	}

	if (pman->state == PKTMAN_DEV_STATE_STOPPING && !pman->nr_ciphering)
		schedule_work(&pman->work);
}

static void
pktman_crypt_complete(struct crypto_async_request *req,
		      int err)
{
	struct pktman_kreq *kreq = req->data;
	struct pktman_dev *pman = kreq->pman;

	spin_lock(&pman->lock);
	__pktman_crypt_complete(req, err);
	spin_unlock(&pman->lock);
}

/*
 * Process a message and either start all of the ciphering or begin the DMA.
 * If we have submessages that need ciphering then we can't do the DMA until
 * they have completed.
 *
 * By dispatching the crypto requests close together we should be able to take
 * more advantage of any hardware support for coalescing process than doing
 * them as soon as they are ready.
 */
static void
pktman_msg_do(struct pktman_dev *pman,
	      struct pktman_msg *msg)
{
	struct ablkcipher_request *ablk_req;
	struct pktman_kreq *pos, *tmp;
	int has_crypt = 1;

	spin_lock_bh(&pman->lock);

	if (list_empty(&msg->children))
		has_crypt = 0;

	list_for_each_entry_safe(pos, tmp, &msg->children, head) {
		pman->nr_ciphering++;
		ablk_req = pktman_kreq_ablkreq(pos);

		/* Now start the ciphering. */
		pos->status = crypto_ablkcipher_encrypt(ablk_req);
		if (-EINPROGRESS != pos->status)
			__pktman_crypt_complete(&ablk_req->base, pos->status);
	}

	/*
	 * There is no ciphering data to complete so we can start DMA. If
	 * there is ciphering data, then the DMA will start when the last one
	 * completes.
	 */
	if (!has_crypt && msg->msg_size) {
		msg->status = 0;
		pktman_push(pman);
	}
	spin_unlock_bh(&pman->lock);
}

static int
pktman_wait_for_space(struct pktman_dev *pman,
		      size_t len,
		      int can_block)
{
	while (pktman_buf_space(&pman->buf) < len) {
		mutex_unlock(&pman->mutex);

		if (!can_block)
			return -ENOSPC;

		if (wait_event_interruptible(pman->waitq,
					pktman_buf_space(&pman->buf) >= len))
			return -ERESTARTSYS;

		if (mutex_lock_interruptible(&pman->mutex))
			return -ERESTARTSYS;
	}

	return 0;
}

static struct pktman_msg *
pktman_start_msg(struct pktman_dev *pman,
		 size_t len)
{
	struct pktman_msg *ret = NULL;

	ret = pktman_msg_alloc(pman);
	if (!ret)
		goto out;

	ret->msg_size = len;

	spin_lock_bh(&pman->lock);
	list_add_tail(&ret->head, &pman->in_progress);
	spin_unlock_bh(&pman->lock);
out:
	return ret;
}

static int
pktman_cryptwrite(struct pktman_dev *pman,
		  struct pktman_msg *msg,
		  const struct pktman_req *req,
		  struct scatterlist *sg)
{
	struct pktman_kreq *kreq;
	int err = 0;
	struct ablkcipher_request *ablk_req;
	struct pktman_key_ctx *ctx;

	if (req->len - req->cip_offs > PKTMAN_MAX_CIPHER_LEN ||
	    0 == req->len - req->cip_offs)
		return -EMSGSIZE;

	if (req->ctx >= pman->nr_key_ctxs || !pman->key_ctxs[req->ctx]) {
		pr_warning("unable to use key context %u\n", req->ctx);
		return -ECHRNG;
	}
	ctx = pman->key_ctxs[req->ctx];

	kreq = pktman_kreq_alloc(ctx->cipher);
	if (!kreq) {
		err = -ENOMEM;
		goto out;
	}
	kreq->msg = msg;
	kreq->pman = pman;
	kreq->status = -EBUSY;
	kreq->ctx = ctx;
	memcpy(kreq->sg, sg, sizeof(kreq->sg));

	/* Configure the encryption request. */
	ablk_req = pktman_kreq_ablkreq(kreq);
	ablkcipher_request_set_tfm(ablk_req, ctx->cipher);
	ablkcipher_request_set_callback(ablk_req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				pktman_crypt_complete, kreq);
	ablkcipher_request_set_crypt(ablk_req, kreq->sg, kreq->sg,
				     req->len - req->cip_offs, kreq->iv);
	memcpy(kreq->iv, req->iv.b8, sizeof(req->iv));

	atomic_inc(&ctx->use_count);

	/*
	 * Add the request to the message and if we have a complete message,
	 * see if we can send it on.
	 */
	spin_lock_bh(&pman->lock);
	list_add_tail(&kreq->head, &msg->children);
	spin_unlock_bh(&pman->lock);

	goto out;

out:
	return err;
}

static inline int
pktman_handle_req(struct pktman_dev *pman,
		  struct pktman_msg *msg,
		  const struct pktman_req *req,
		  struct scatterlist *sg)
{
	int err;

	switch (req->op) {
	case PKTMAN_OP_SETKEY:
		err = pktman_setkey(pman, req, sg);
		break;

	case PKTMAN_OP_CRYPTWRITE:
		err = pktman_cryptwrite(pman, msg, req, sg);
		break;

	case PKTMAN_OP_WRITETHROUGH:
		/*
		 * We don't need to do anything here - the data is in the
		 * buffer so as soon as the message is complete it can be
		 * transferred into the picoArray.
		 */
		err = 0;
		break;

	default:
		err = -ENOTTY;
	}

	return err;
}

enum pktman_msg_group {
	PKTMAN_MSG_GROUP_INVALID,
	PKTMAN_MSG_GROUP_SETKEY,
	PKTMAN_MSG_GROUP_WRITE,
};

static enum pktman_msg_group
pktman_reqs_group(struct pktman_req *reqs,
		  int nr_reqs,
		  size_t data_len)
{
	int have_setkey = 0, have_write = 0, i;
	size_t total_data = 0;
	enum pktman_msg_group ret = PKTMAN_MSG_GROUP_INVALID;

	for (i = 0; i < nr_reqs; ++i)
		total_data += reqs[i].len;

	if (total_data > data_len) {
		pr_warning("insufficient data to process reqs (%zu/%zu)\n",
			   data_len, total_data);
		goto out;
	}

	/*
	 * Check that the requests don't contain write operations and a key
	 * change. We don't support this as the key will end up in the buffer
	 * to be written to the picoArray.
	 */
	for (i = 0; i < nr_reqs; ++i) {
		if (PKTMAN_OP_SETKEY == reqs[i].op)
			have_setkey = 1;
		else if (PKTMAN_OP_NONE != reqs[i].op)
			have_write = 1;
	}

	if (have_write && have_setkey) {
		pr_warning("request contains mixed key change/write operations\n");
		ret = PKTMAN_MSG_GROUP_INVALID;
	} else if (have_write) {
		ret = PKTMAN_MSG_GROUP_WRITE;
	} else {
		ret = PKTMAN_MSG_GROUP_SETKEY;
	}

out:
	return ret;
}

/*
 * Given an offset in the FIFO and a length, create a scatterlist mapping the
 * data. The scatterlist must be already allocated and at least 2 entries long
 * to cope with the FIFO wrapping.
 *
 * @pman The packetmanager containing the FIFO.
 * @offs The starting offset of the data in the FIFO.
 * @len The length of the data to put in the scatterlist.
 * @sg The scatterlist to populate.
 */
static void
pktman_fill_sg(struct pktman_dev *pman,
	       unsigned offs,
	       unsigned cip_offs,
	       size_t len,
	       struct scatterlist *sg)
{
	size_t l1, l2;
	unsigned real_offs = pktman_buf_offset(&pman->buf, offs + cip_offs);

	l1 = min(len, pman->buf.buf_sz - real_offs);
	l2 = len - l1;

	if (l2) {
		sg_init_table(sg, 2);
		sg_set_buf(&sg[0], pman->buf.buf + real_offs, l1);
		sg_set_buf(&sg[1], pman->buf.buf, l2);
	} else {
		sg_init_one(sg, pman->buf.buf + real_offs, l1);
	}
}

/*
 * Abort a message that hasn't yet started processing. This means removing all
 * of the cryptwrite requests and freeing the memory.
 */
static void
pktman_abort_msg(struct pktman_dev *pman,
		 struct pktman_msg *msg)
{
	struct pktman_kreq *pos, *tmp;

	/* No locking required as no crypto operations have been started. */
	list_for_each_entry_safe(pos, tmp, &msg->children, head) {
		atomic_dec(&pos->ctx->use_count);
		list_del(&pos->head);
		kfree(pos);
	}

	list_del(&msg->head);
	kfree(msg);
}

static int
pktman_handle_msg(struct file *filp,
		  size_t reqs_len,
		  size_t data_len,
		  unsigned start_idx)
{
	struct pktman_dev *pman = filp->private_data;
	struct scatterlist sg[2];
	ssize_t ret = 0;
	unsigned old_start_idx = start_idx;
	struct pktman_msg *msg = NULL;
	int i, nr_reqs;
	enum pktman_msg_group group_type;

	nr_reqs = reqs_len / sizeof(struct pktman_req);

	/* Check that we have a valid combination of messages. */
	group_type = pktman_reqs_group(pman->req_bounce_buf, nr_reqs, data_len);
	if (PKTMAN_MSG_GROUP_INVALID == group_type) {
		pr_warning("invalid combination of requests\n");
		ret = -EINVAL;
		goto out;
	}

	if (PKTMAN_MSG_GROUP_SETKEY != group_type) {
		msg = pktman_start_msg(pman, roundup(data_len,
						     PKTMAN_PKT_PADDING));
		if (!msg) {
			ret = -ENOMEM;
			goto out;
		}
	}

	/*
	 * Process the operations in order. If we run out of FIFO space for
	 * new packets then we return early so that we don't block the
	 * application for too long.
	 *
	 * Note: key changes *will* block and they may take some time.  We
	 * need to block for key changes to make sure that any in-flight
	 * operations get to use the old key and that we preserve the ordering
	 * of messages. The key here (pun intended) is to make sure that there
	 * are sufficient key contexts that the changing of the keys is
	 * infrequent.
	 */
	for (i = 0; i < nr_reqs; ++i) {
		struct pktman_req *req = &pman->req_bounce_buf[i];

		pktman_fill_sg(pman, start_idx, req->cip_offs,
			       req->len - req->cip_offs, sg);

		ret = pktman_handle_req(pman, msg, req, sg);
		if (ret)
			goto out_abort_msg;

		start_idx += req->len;
	}

	/*
	 * If we have some operations that write data then write the data. If
	 * the requests were just key change requests then we need to remove
	 * the data from the FIFO so we don't transfer the keys as messages.
	 */
	if (PKTMAN_MSG_GROUP_SETKEY != group_type)
		pktman_msg_do(pman, msg);
	else
		pman->buf.wptr = old_start_idx;

	ret = data_len + reqs_len;
	goto out;

out_abort_msg:
	if (msg)
		pktman_abort_msg(pman, msg);
out:

	if (0 == ret && (filp->f_flags & O_NONBLOCK))
		ret = -EAGAIN;
	return ret;
}

static ssize_t
pktman_aio_write(struct kiocb *iocb,
		 const struct iovec *vecs,
		 unsigned long nr_segs,
		 loff_t offs)
{
	const char __user *reqs;
	size_t reqs_len, data_len = 0;
	unsigned seg, start_idx;
	ssize_t ret = 0;
	int can_block = !(iocb->ki_filp->f_flags & O_NONBLOCK);
	struct pktman_dev *pman = iocb->ki_filp->private_data;

	/*
	 * The first segment is the requests and any following segments are
	 * the data. We need at least 2 segments otherwise we can't do
	 * anything.
	 */
	if (nr_segs < 2) {
		pr_warning("invalid number of io_vecs (%lu)\n", nr_segs);
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&pman->mutex))
		return -ERESTARTSYS;

	reqs	    = vecs[0].iov_base;
	reqs_len    = vecs[0].iov_len;

	if (reqs_len >= PKTMAN_MAX_REQS_PER_IOVEC * sizeof(struct pktman_req)) {
		pr_warning("too many requests to service\n");
		ret = -EINVAL;
		goto out;
	}

	if (copy_from_user(pman->req_bounce_buf, reqs, reqs_len)) {
		ret = -EFAULT;
		goto out;
	}

	for (seg = 1; seg < nr_segs; ++seg)
		data_len += vecs[seg].iov_len;
	/*
	 * Make sure that we can fit all of the data into the FIFO. We don't
	 * do partial messages as we may not be able to satisfy the cache
	 * alignment requirements for DMA.
	 */
	ret = pktman_wait_for_space(pman, data_len, can_block);
	if (ret)
		return ret;

	for (seg = 1; seg < nr_segs; ++seg) {
		ret = pktman_buf_add(&pman->buf, vecs[seg].iov_base,
				     vecs[seg].iov_len,
				     1 == seg ? &start_idx : NULL,
				     seg == nr_segs - 1);
		if (ret < 0)
			goto err_add;
	}

	ret = pktman_handle_msg(iocb->ki_filp, reqs_len, data_len,
				start_idx);
	goto out;

err_add:
	pman->buf.wptr = start_idx;
out:
	mutex_unlock(&pman->mutex);

	return ret;
}

static unsigned int
pktman_poll(struct file *filp,
	    struct poll_table_struct *pollt)
{
	struct pktman_dev *pman = filp->private_data;
	unsigned int ret = 0;

	poll_wait(filp, &pman->waitq, pollt);

	if (pktman_buf_space(&pman->buf) >= pman->poll_wr_thresh)
		ret |= POLLOUT;

	return ret;
}

static const struct file_operations pktman_fops = {
	.open		    = pktman_open,
	.aio_write	    = pktman_aio_write,
	.poll		    = pktman_poll,
	.release	    = pktman_release,
	.owner		    = THIS_MODULE,
};

static void pktman_dev_cleanup(struct work_struct *work)
{
	struct pktman_dev *pman = container_of(work, struct pktman_dev, work);

	config_item_put(&pman->item);
}

static void
pktman_dev_release(struct device *dev)
{
	struct pktman_dev *pman = to_pktman(dev);

	pman->state = PKTMAN_DEV_STATE_IDLE;
	pktman_free_all_ctxs(pman);
	cdev_del(&pman->cdev);

	list_del(&pman->head);
	if (pman->picoif)
		picoif_directdma_close(pman->picoif);
	kfree(pman->buf.buf);

	clear_bit(pman->id, pktman.present_map);
	vfree(pman->req_bounce_buf);
	kfree(pman);
}

static struct pktman_dev *
pktman_add(int id)
{
	struct pktman_dev *pman = kzalloc(sizeof(*pman), GFP_KERNEL);
	dev_t devno = MKDEV(MAJOR(pktman.devno), id);
	int err = -ENOMEM;

	if (!pman)
		goto out;

	if (pktman_buf_init(&pman->buf))
		goto err_buf;

	pman->id	    = id;
	pman->dev.parent    = NULL;
	pman->dev.release   = pktman_dev_release;
	pman->dev.class	    = pktman.class;
	pman->dev.devt	    = devno;
	pman->key_ctxs	    = NULL;
	pman->nr_key_ctxs   = 0;
	pman->state	    = PKTMAN_DEV_STATE_IDLE;
	pman->picoif	    = NULL;
	pman->dma_channel   = 0;
	pman->nr_ciphering  = 0;
	pman->poll_wr_thresh= PKTMAN_BUF_SZ / 16;
	pman->req_bounce_buf = vmalloc(sizeof(*pman->req_bounce_buf) *
			PKTMAN_MAX_REQS_PER_IOVEC);
	if (!pman->req_bounce_buf)
		goto err_buf;

	atomic_set(&pman->use_count, 0);
	dev_set_name(&pman->dev, "pktman%d", id);
	list_add_tail(&pman->head, &pktman.devices);
	mutex_init(&pman->mutex);
	spin_lock_init(&pman->lock);
	INIT_LIST_HEAD(&pman->in_progress);
	INIT_WORK(&pman->work, pktman_dev_cleanup);
	init_waitqueue_head(&pman->waitq);
	sg_init_table(pman->xfer_sg, 2);

	cdev_init(&pman->cdev, &pktman_fops);
	err = device_register(&pman->dev);
	if (err)
		goto err_dev;

	err = cdev_add(&pman->cdev, devno, 1);
	if (err)
		goto err_cdev;

	goto out;

err_cdev:
	device_del(&pman->dev);
err_dev:
	kfree(pman->buf.buf);
err_buf:
	kfree(pman);
out:
	return err ? ERR_PTR(err) : pman;
}

CONFIGFS_ATTR_STRUCT(pktman_dev);
#define PKTMAN_ATTR(__name, __mode, __show, __store) \
	struct pktman_dev_attribute pktman_attr_##__name = \
	__CONFIGFS_ATTR(__name, __mode, __show, __store)

#define PKTMAN_ATTR_RO(__name, __show) \
	struct pktman_dev_attribute pktman_attr_##__name = \
	__CONFIGFS_ATTR_RO(__name, __show)

static inline struct pktman_dev *
to_pktman_dev(struct config_item *item)
{
	return item ? container_of(item, struct pktman_dev, item) : NULL;
}

CONFIGFS_ATTR_OPS(pktman_dev);

static ssize_t
pktman_channel_show(struct pktman_dev *pman,
		    char *page)
{
	return snprintf(page, PAGE_SIZE, "%d\n", pman->id);
}
PKTMAN_ATTR_RO(channel, pktman_channel_show);

static ssize_t
pktman_max_reqs_per_iovec_show(struct pktman_dev *pman,
			       char *page)
{
	return snprintf(page, PAGE_SIZE, "%d\n", PKTMAN_MAX_REQS_PER_IOVEC);
}
PKTMAN_ATTR_RO(max_reqs_per_iovec, pktman_max_reqs_per_iovec_show);

static ssize_t
pktman_dma_channel_show(struct pktman_dev *pman,
			char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", pman->dma_channel);
}

static ssize_t
pktman_dma_channel_store(struct pktman_dev *pman,
			 const char *page,
			 size_t len)
{
	unsigned dma_channel = simple_strtoul(page, NULL, 0);
	int ret = 0;

	if (mutex_lock_interruptible(&pman->mutex))
		return -ERESTARTSYS;

	if (atomic_read(&pman->use_count))
		ret = -EBUSY;
	else
		pman->dma_channel = dma_channel;

	mutex_unlock(&pman->mutex);

	return ret ?: len;
}
PKTMAN_ATTR(dma_channel, 0600, pktman_dma_channel_show,
	    pktman_dma_channel_store);

static ssize_t
pktman_poll_wr_thresh_show(struct pktman_dev *pman,
			   char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", pman->poll_wr_thresh);
}

static ssize_t
pktman_poll_wr_thresh_store(struct pktman_dev *pman,
			    const char *page,
			    size_t len)
{
	unsigned poll_wr_thresh = simple_strtoul(page, NULL, 0);

	if (0 == poll_wr_thresh || poll_wr_thresh >= PKTMAN_BUF_SZ)
		return -EINVAL;

	pman->poll_wr_thresh = poll_wr_thresh;

	return len;
}
PKTMAN_ATTR(poll_wr_thresh, 0600, pktman_poll_wr_thresh_show,
	    pktman_poll_wr_thresh_store);

static ssize_t
pktman_fifo_sz_show(struct pktman_dev *pman,
		    char *page)
{
	return snprintf(page, PAGE_SIZE, "%zu\n", pman->buf.buf_sz);
}
PKTMAN_ATTR_RO(fifo_sz, pktman_fifo_sz_show);

static struct configfs_attribute *pktman_attrs[] = {
	&pktman_attr_dma_channel.attr,
	&pktman_attr_channel.attr,
	&pktman_attr_fifo_sz.attr,
	&pktman_attr_max_reqs_per_iovec.attr,
	&pktman_attr_poll_wr_thresh.attr,
	NULL,
};

static void
pktman_release_item(struct config_item *item)
{
	struct pktman_dev *pman = to_pktman_dev(item);

	while (pman->nr_ciphering)
		cpu_relax();

	put_device(&pman->dev);
	device_unregister(&pman->dev);
}

static struct configfs_item_operations pktman_item_ops = {
	.show_attribute	    = pktman_dev_attr_show,
	.store_attribute    = pktman_dev_attr_store,
	.release	    = pktman_release_item,
};

static struct config_item_type pktman_type = {
	.ct_item_ops	    = &pktman_item_ops,
	.ct_attrs	    = pktman_attrs,
	.ct_owner	    = THIS_MODULE,
};

struct pktman_group {
	struct config_group	group;
};

static struct config_item *
pktman_make_item(struct config_group *group,
		 const char *name)
{
	struct pktman_dev *pman = ERR_PTR(-ENODEV);
	int id, err = 0;

	do {
		id = find_first_zero_bit(pktman.present_map,
					 PKTMAN_MAX_DEVICES);
		if (id >= PKTMAN_MAX_DEVICES)
			return ERR_PTR(-ENOSPC);
	} while (test_and_set_bit(id, pktman.present_map));

	pman = pktman_add(id);
	if (IS_ERR(pman)) {
		clear_bit(id, pktman.present_map);
		err = PTR_ERR(pman);
		goto out;
	}
	get_device(&pman->dev);
	config_item_init_type_name(&pman->item, name, &pktman_type);

out:
	return err ? ERR_PTR(err) : &pman->item;
}

static struct configfs_group_operations pktman_group_ops = {
	.make_item	    = pktman_make_item,
};

static inline struct pktman_group *
to_pktman_group(struct config_item *item)
{
	return item ? container_of(to_config_group(item),
				   struct pktman_group, group) : NULL;
}

static void
pktman_group_release(struct config_item *item)
{
	kfree(to_pktman_group(item));
}

static struct configfs_item_operations pktman_group_item_ops = {
	.release	    = pktman_group_release,
};

static struct config_item_type pktman_group_type = {
	.ct_item_ops	    = &pktman_group_item_ops,
	.ct_group_ops	    = &pktman_group_ops,
	.ct_owner	    = THIS_MODULE,
};

static struct configfs_subsystem pktman_subsys = {
	.su_group		    = {
		.cg_item	    = {
			.ci_namebuf = "pktman",
			.ci_type    = &pktman_group_type,
		},
	},
};

static int __init
pktman_init(void)
{
	int err;

	pr_info("packetmanager © 2010 picoChip\n");

	err = alloc_chrdev_region(&pktman.devno, 0, PKTMAN_MAX_DEVICES,
				  "pktman");
	if (err)
		return err;

	pktman.class = class_create(THIS_MODULE, "pktman");
	if (IS_ERR(pktman.class)) {
		err = PTR_ERR(pktman.class);
		goto err_class;
	}

	config_group_init(&pktman_subsys.su_group);
	mutex_init(&pktman_subsys.su_mutex);
	err = configfs_register_subsystem(&pktman_subsys);
	if (err)
		goto err_configfs;

	goto out;

err_configfs:
	class_destroy(pktman.class);
err_class:
	unregister_chrdev_region(pktman.devno, PKTMAN_MAX_DEVICES);
out:
	return err;
}

static void
pktman_exit(void)
{
	configfs_unregister_subsystem(&pktman_subsys);
	class_destroy(pktman.class);
	unregister_chrdev_region(pktman.devno, PKTMAN_MAX_DEVICES);
}

module_init(pktman_init);
module_exit(pktman_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("picoChip WCDMA Layer2 ciphering/DMA packet engine");
