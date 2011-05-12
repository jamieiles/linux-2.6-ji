/**
 * \file packetmanager.h
 * \brief The F8 Packet manager ABI and userspace helper functions.
 *
 * Copyright (c) 2010 picoChip Designs Ltd
 * Proprietary and Confidential Information.
 * Not to be copied or distributed.
 *
 * All enquiries to support@picochip.com
 *
 * \author Jamie Iles
 * \mainpage
 *
 * \section introduction Introduction
 *
 * The packetmanager provides a mechanism for userspace applications to cipher
 * data and DMA both plaintext and ciphertext into the picoArray with as
 * little CPU overhead as possible. This packet manager copies data from
 * userspace into a kernel buffer then performs ciphering in place before
 * DMAing from the same buffer into the picoArray via picoIf. This involves
 * only one data copy and allows asynchronous ciphering.
 *
 * The packetmanager supports the following features:
 *
 *        - Up to 8 simultaneous instances (limited by the number of physical
 *          DMA channels to the picoArray).
 *        - An unlimited number of ciphering key contexts per packetmanager
 *          instance.
 *        - A small helper API for request formatting.
 *        - A POSIX file descriptor interface that allows blocking and
 *          non-blocking I/O.
 *
 * \section configuration Configuration
 *
 * Packet manager instances are configured using configfs. If configfs is
 * mounted at /config, then "mkdir /config/pktman/pman1" will create a new
 * packetmanager instance called "pman1". Once created, the packetmanager will
 * contain 4 attributes:
 *
 * \code
 * root@pc7302:~# tree /config/pktman/pman1/ -p
 * /config/pktman/pman1/
 * |-- [-r--r--r--]  channel
 * |-- [-rw-------]  dma_channel
 * |-- [-r--r--r--]  fifo_sz
 * |-- [-r--r--r--]  max_reqs_per_iovec
 * `-- [-rw-------]  poll_wr_thresh
 *
 * 0 directories, 5 files
 * \endcode
 *
 * For each packet manager, the \b channel attribute indicates the
 * corresponding \b /dev/pktmanN node that should be used for accessing the
 * packetmanager where \b N corresponds to the value of the \b channel
 * attribute. This is a read-only attribute and will be set automatically by
 * the packetmanager backend.
 *
 * The \b dma_channel attribute is a read-write attribute that sets the DMA
 * channel to use for the DMA into the picoArray.
 *
 * The \b fifo_sz attribute shows the size of the internal FIFO used for
 * storing the packets. This is provided as a reference to the user to ensure
 * that messages that will not fit into the FIFO are appropriately split.
 *
 * The max_reqs_per_iovec shows the maximum number of request structures that
 * may be put into a single iovec. Exceeding this number will result in the
 * write failing with errno = EINVAL.
 *
 * The poll_wr_thresh attribute indicates the number of bytes that must be
 * free in the internal FIFO for poll(2) on the device node to return POLLOUT.
 * This can be used to prevent the application from spinning in the case where
 * it wants to write to the FIFO and there is some space left but not enough
 * for the whole message.
 *
 * \note The devices are created dynamically when a new packetmanager instance
 * is created and this is handled by the dynamic device daemon (mdev) to
 * create the corresponding device node. If your platform does not use
 * mdev/udev then you will need to create the device nodes manually.
 *
 * \note The packetmanager takes exclusive use of the DMA channel specified by
 * the \b dma_channel attribute so it may not be shared with another picoIf
 * instance.
 *
 * \section using Using the Packet Manager
 *
 * Once the packetmanager has been configured, it is used by opening the
 * corresponding device node for writing. Once the packet manager has been
 * finished with, it may be closed - ciphering and transfers in progress will
 * be completed but extra data in the FIFO will be discarded.
 *
 * The packetmanager is controlled using struct pktman_req structures
 * initialized with helper functions then written to the device with write(2).
 * The three operations supported are:
 *
 *        - \b setkey (pktman_prep_setkey()) - create a new key context or
 *          modify an existing context to use a new key.
 *        - \b cryptwrite (pktman_prep_cryptwrite()) - cipher some data then DMA
 *          into the picoArray.
 *        - \b writethrough (pktman_prep_writethrough()) - write some data
 *          straight into the picoArray without ciphering.
 *
 * Multiple requests may be written into the device at a time and the
 * packetmanager exhibits normal writev(2) semantics with regards to blocking
 * and partial writes. Requests are performed strictly in order and once
 * writev() returns, any buffers associated with dispatched requests may be
 * freed/reused. Additionally, the file descriptor may be polled for POLLOUT
 * to indicate when new messages may be written without blocking.
 *
 * Note: Due to the internal FIFO structure, PKTMAN_OP_SETKEY requests must
 * not be mixed with any other operation type in the same writev() call.
 *
 * For example, to set the key of a packetmanager instance and cipher some
 * data:
 *
 * \code
 * static void
 * pktman_example(int fd)
 * {
 *         char key[16], buf[128];
 *         struct pktman_req req;
 *         struct iovec iov[2] = {
 *                 { .iov_base = &req, .iov_len = sizeof(req) },
 *                 { .iov_base = key, .iov_len = sizeof(key) },
 *         };
 *         int ret;
 *
 *         // Create key context 0 and set the key.
 *         pktman_prep_setkey(&req, 0, sizeof(key));
 *	   // Do the key change.
 *         ret = writev(fd, iov, 2);
 *         assert(sizeof(key) + sizeof(req) == ret);
 *
 *         // Cipher a 128 byte message where the first 32 bytes are
 *         // plaintext. Use key context 0 with a CountC, radio bearer ID and
 *         // direction of 0x1234, 0x10 and 1 respectively.
 *         iov[1].iov_base = buf;
 *         iov[1].iov_len = sizeof(buf);
 *         pktman_prep_cryptwrite(&req, 0, sizeof(buf), 32,
 *                                0x1234, 0x10, 1);
 *
 *         // Write the request to cipher the data.
 *         ret = writev(fd, iov, 2);
 *         assert(sizeof(buf) + sizeof(req) == ret);
 * }
 * \endcode
 *
 * Calls to writev(2) must always put the requests in the first iovec
 * structure and the data in following iovecs. The data in iovecs following
 * the requests must be formatted in the order of the requests.
 *
 * \code
 *        struct pdu {
 *                char data[128];
 *        };
 *        struct pdu msg[2];
 *        struct pktman_req reqs[2];
 *        struct iovec iov[2] = {
 *                { .iov_base = reqs, .iov_len = sizeof(reqs) },
 *                { .iov_base = &msg[0], .iov_len = sizeof(msg[0]) },
 *                { .iov_base = &msg[1], .iov_len = sizeof(msg[1]) },
 *        };
 * \code
 */
#ifndef __PACKETMANAGER_H__
#define __PACKETMANAGER_H__

#ifndef __KERNEL__
#define __user
#endif /* !__KERNEL__ */

#include <linux/types.h>

/**
 * @brief The operations supported by the packet manager backend.
 */
enum pktman_op {
	PKTMAN_OP_NONE,			/**< NOP - do nothing and skip. */
	PKTMAN_OP_SETKEY,		/**< Change the key for a context. */
	PKTMAN_OP_CRYPTWRITE,	    	/**< Cipher then write. */
	PKTMAN_OP_WRITETHROUGH,	    	/**< Write without ciphering. */
};

/**
 * @brief Convert 2x32-bit integers into a byte array without typecasting.
 */
union iv_converter {
	__u32	    b32[2];		/**< The 32-bit quantities. */
	__u8	    b8[8];		/**< The 8-bit quantities. */
};

/**
 * @brief The request structure for control or a segment of a packet.
 *
 * These structures are used for passing operations to the packetmanager
 * backend. These structures should not be filled out directly, instead, use
 * the pktman_prep_*() helper functions.
 */
struct pktman_req {
	__u8		    op;		/**< The (enum pktman_op) to perform. */
	__u16		    ctx;    	/**< The context ID to use. */
	__u16		    len;    	/**< The length of the buffer. */
	__u16		    cip_offs;	/**< The ciphering offset. */
	union iv_converter  iv;  	/**< The IV for ciphering operations. */
};

#ifndef __KERNEL__

#include <string.h>
#include <arpa/inet.h>

/**
 * Prepare a request to change the key for a key context or create a new one.
 *
 * Note: when changing the key for an existing context, this operation will
 * block until all previous PKTMAN_OP_CRYPTWRITE operations have completed so
 * this is safe to dispatch at any time without manually waiting for requests
 * to complete.
 *
 * @param req The request to initialize.
 * @param ctx The key context identifier. If this identifier has already been
 *	used since creating the packetmanager instance then it will change the
 *	key, otherwise it will create a new key context.
 * @param key_len The length of the key in bytes.
 */
static inline void
pktman_prep_setkey(struct pktman_req *req,
		   unsigned ctx,
		   size_t key_len)
{
	req->op		    = PKTMAN_OP_SETKEY;
	req->ctx    	    = ctx;
	req->len    	    = key_len;
	req->cip_offs	    = 0;
}

/**
 * Prepare a request structure to encrypt a message and DMA into the
 * picoArray.
 *
 * This operation will transfer the data into the packetmanager backend then
 * queue the relevant portion of the message for ciphering. After completion,
 * the message will be transferred into the picoArray.
 *
 * @param req The request to initialize.
 * @param ctx The key context to use for the ciphering. This must have been
 *	previously created with a request initialized by pktman_prep_setkey().
 * @param data_len The length of the data buffer in bytes including the
 *	plaintext and ciphertext.
 * @param cip_offset The offset from the beginning message in bytes of the
 *	portion that should be ciphered.
 * @param count_c The CountC value to use for the ciphering.
 * @param rbid The radio bearer ID. Only the 5 LSB's are used.
 * @param direction The direction to perform the ciphering with. This should
 *	be either 0 or 1.
 */
static inline void
pktman_prep_cryptwrite(struct pktman_req *req,
		       unsigned ctx,
		       size_t data_len,
		       unsigned cip_offset,
		       int count_c,
		       int rbid,
		       int direction)
{
	req->op		    = PKTMAN_OP_CRYPTWRITE;
	req->ctx	    = ctx;
	req->len	    = data_len;
	req->cip_offs	    = cip_offset;
	req->iv.b32[1]	    = ntohl(((rbid & 0x1F) << 27) |
				    ((direction & 1) << 26));
	req->iv.b32[0]	    = ntohl(count_c);
}

/**
 * Prepare a request structure to DMA a message into the picoArray without any
 * ciphering.
 *
 * @param req The request to initialize.
 * @param data_len The length of the data buffer in bytes.
 */
static inline void
pktman_prep_writethrough(struct pktman_req *req,
		  	 size_t data_len)
{
	req->op		    = PKTMAN_OP_WRITETHROUGH;
	req->len	    = data_len;
	req->cip_offs	    = 0;
}
		   
#endif /* !__KERNEL__ */

#endif /* __PACKETMANAGER_H__ */
