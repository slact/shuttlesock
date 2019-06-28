/*
 * io_uring_register.c
 *
 * Description: Unit tests for the io_uring_register system call.
 *
 * Copyright 2019, Red Hat, Inc.
 * Author: Jeff Moyer <jmoyer@redhat.com>
 */
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/sysinfo.h>
#include <poll.h>
#include <assert.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <linux/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <limits.h>
#include "../src/liburing.h"

static int pagesize;
static rlim_t mlock_limit;
static int devnull;

int
expect_fail(int fd, unsigned int opcode, void *arg,
	    unsigned int nr_args, int error)
{
	int ret;

	printf("io_uring_register(%d, %u, %p, %u)\n",
	       fd, opcode, arg, nr_args);
	ret = io_uring_register(fd, opcode, arg, nr_args);
	if (ret != -1) {
		int ret2 = 0;

		printf("expected %s, but call succeeded\n", strerror(error));
		if (opcode == IORING_REGISTER_BUFFERS) {
			ret2 = io_uring_register(fd, IORING_UNREGISTER_BUFFERS,
						 0, 0);
		} else if (opcode == IORING_REGISTER_FILES) {
			ret2 = io_uring_register(fd, IORING_UNREGISTER_FILES,
						 0, 0);
		}
		if (ret2) {
			printf("internal error: failed to unregister\n");
			exit(1);
		}
		return 1;
	}

	if (errno != error) {
		printf("expected %d, got %d\n", error, errno);
		return 1;
	}
	return 0;
}

int
new_io_uring(int entries, struct io_uring_params *p)
{
	int fd;

	fd = io_uring_setup(entries, p);
	if (fd < 0) {
		perror("io_uring_setup");
		exit(1);
	}
	return fd;
}

#define MAXFDS (UINT_MAX * sizeof(int))

void *
map_filebacked(size_t size)
{
	int fd, ret;
	void *addr;
	char template[32] = "io_uring_register-test-XXXXXXXX";

	fd = mkstemp(template);
	if (fd < 0) {
		perror("mkstemp");
		return NULL;
	}
	unlink(template);

	ret = ftruncate(fd, size);
	if (ret < 0) {
		perror("ftruncate");
		close(fd);
		return NULL;
	}

	addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		close(fd);
		return NULL;
	}

	close(fd);
	return addr;
}

/*
 * NOTE: this is now limited by SCM_MAX_FD (253).  Keep the code for now,
 * but probably should augment it to test 253 and 254, specifically.
 */
int
test_max_fds(int uring_fd)
{
	int status = 1;
	int ret;
	void *fd_as; /* file descriptor address space */
	int fdtable_fd; /* fd for the file that will be mapped over and over */
	int io_fd; /* the valid fd for I/O -- /dev/null */
	int *fds; /* used to map the file into the address space */
	char template[32] = "io_uring_register-test-XXXXXXXX";
	unsigned long long i, nr_maps, nr_fds;

	/*
	 * First, mmap anonymous the full size.  That will guarantee the
	 * mapping will fit in the memory area selected by mmap.  Then,
	 * over-write that mapping using a file-backed mapping, 128MiB at
	 * a time using MAP_FIXED.
	 */
	fd_as = mmap(NULL, UINT_MAX * sizeof(int), PROT_READ|PROT_WRITE,
		     MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (fd_as == MAP_FAILED) {
		if (errno == ENOMEM) {
			printf("Not enough memory for this test, skipping\n");
			return 0;
		}
		perror("mmap fd_as");
		exit(1);
	}
	printf("allocated %lu bytes of address space\n", UINT_MAX * sizeof(int));

	fdtable_fd = mkstemp(template);
	if (fdtable_fd < 0) {
		perror("mkstemp");
		exit(1);
	}
	unlink(template);
	ret = ftruncate(fdtable_fd, 128*1024*1024);
	if (ret < 0) {
		perror("ftruncate");
		exit(1);
	}

	io_fd = open("/dev/null", O_RDWR);
	if (io_fd < 0) {
		perror("open /dev/null");
		exit(1);
	}
	fds = mmap(fd_as, 128*1024*1024, PROT_READ|PROT_WRITE,
		   MAP_SHARED|MAP_FIXED, fdtable_fd, 0);
	if (fds == MAP_FAILED) {
		perror("mmap fdtable");
		exit(1);
	}

	/* fill the fd table */
	nr_fds = 128*1024*1024 / sizeof(int);
	for (i = 0; i < nr_fds; i++)
		fds[i] = io_fd;

	/* map the file through the rest of the address space */
	nr_maps = (UINT_MAX * sizeof(int)) / (128*1024*1024);
	for (i = 0; i < nr_maps; i++) {
		fds = &fds[nr_fds]; /* advance fds by 128MiB */
		fds = mmap(fds, 128*1024*1024, PROT_READ|PROT_WRITE,
			   MAP_SHARED|MAP_FIXED, fdtable_fd, 0);
		if (fds == MAP_FAILED) {
			printf("mmap failed at offset %lu\n", (char *)fd_as - (char *)fds);
			exit(1);
		}
	}

	/* Now fd_as points to the file descriptor array. */
	/*
	 * We may not be able to map all of these files.  Let's back off
	 * until success.
	 */
	nr_fds = UINT_MAX;
	while (nr_fds) {
		ret = io_uring_register(uring_fd, IORING_REGISTER_FILES,
					fd_as, nr_fds);
		if (ret != 0) {
			nr_fds /= 2;
			continue;
		}
		printf("io_uring_register(%d, IORING_REGISTER_FILES, %p, %llu)"
		       "...succeeded\n", uring_fd, fd_as, nr_fds);
		status = 0;
		printf("io_uring_register(%d, IORING_UNREGISTER_FILES, 0, 0)...",
		       uring_fd);
		ret = io_uring_register(uring_fd, IORING_UNREGISTER_FILES, 0, 0);
		if (ret < 0) {
			ret = errno;
			printf("failed\n");
			errno = ret;
			perror("io_uring_register UNREGISTER_FILES");
			exit(1);
		}
		printf("succeeded\n");
		break;
	}

	close(io_fd);
	close(fdtable_fd);
	ret = munmap(fd_as, UINT_MAX * sizeof(int));
	if (ret != 0) {
		printf("munmap(%lu) failed\n", UINT_MAX * sizeof(int));
		exit(1);
	}

	return status;
}

int
test_memlock_exceeded(int fd)
{
	int ret;
	void *buf;
	struct iovec iov;

	/* if limit is larger than 2gb, just skip this test */
	if (mlock_limit >= 2 * 1024 * 1024 * 1024ULL)
		return 0;

	iov.iov_len = mlock_limit * 2;
	buf = malloc(iov.iov_len);
	assert(buf);
	iov.iov_base = buf;

	while (iov.iov_len) {
		ret = io_uring_register(fd, IORING_REGISTER_BUFFERS, &iov, 1);
		if (ret < 0) {
			if (errno == ENOMEM) {
				printf("io_uring_register of %lu bytes failed "
				       "with ENOMEM (expected).\n", iov.iov_len);
				iov.iov_len /= 2;
				continue;
			}
			printf("expected success or EFAULT, got %d\n", errno);
			free(buf);
			return 1;
		}
		printf("successfully registered %lu bytes (%d).\n",
		       iov.iov_len, ret);
		ret = io_uring_register(fd, IORING_UNREGISTER_BUFFERS, NULL, 0);
		if (ret != 0) {
			printf("error: unregister failed with %d\n", errno);
			free(buf);
			return 1;
		}
		break;
	}
	if (!iov.iov_len)
		printf("Unable to register buffers.  Check memlock rlimit.\n");

	free(buf);
	return 0;
}

int
test_iovec_nr(int fd)
{
	int i, ret, status = 0;
	unsigned int nr = UIO_MAXIOV + 1;
	struct iovec *iovs;
	void *buf;

	buf = malloc(pagesize);
	assert(buf);

	iovs = malloc(nr * sizeof(struct iovec));
	assert(iovs);

	for (i = 0; i < nr; i++) {
		iovs[i].iov_base = buf;
		iovs[i].iov_len = pagesize;
	}

	status |= expect_fail(fd, IORING_REGISTER_BUFFERS, iovs, nr, EINVAL);

	/* reduce to UIO_MAXIOV */
	nr--;
	printf("io_uring_register(%d, %u, %p, %u)\n",
	       fd, IORING_REGISTER_BUFFERS, iovs, nr);
	ret = io_uring_register(fd, IORING_REGISTER_BUFFERS, iovs, nr);
	if (ret != 0) {
		printf("expected success, got %d\n", errno);
		status = 1;
	} else
		io_uring_register(fd, IORING_UNREGISTER_BUFFERS, 0, 0);

	free(buf);
	free(iovs);
	return status;
}

/*
 * io_uring limit is 1G.  iov_len limit is ~OUL, I think
 */
int
test_iovec_size(int fd)
{
	unsigned int status = 0;
	int ret;
	struct iovec iov;
	void *buf;

	/* NULL pointer for base */
	iov.iov_base = 0;
	iov.iov_len = 4096;
	status |= expect_fail(fd, IORING_REGISTER_BUFFERS, &iov, 1, EFAULT);

	/* valid base, 0 length */
	iov.iov_base = &buf;
	iov.iov_len = 0;
	status |= expect_fail(fd, IORING_REGISTER_BUFFERS, &iov, 1, EFAULT);

	/* valid base, length exceeds size */
	/* this requires an unampped page directly after buf */
	buf = mmap(NULL, 2 * pagesize, PROT_READ|PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(buf != MAP_FAILED);
	ret = munmap(buf + pagesize, pagesize);
	assert(ret == 0);
	iov.iov_base = buf;
	iov.iov_len = 2 * pagesize;
	status |= expect_fail(fd, IORING_REGISTER_BUFFERS, &iov, 1, EFAULT);
	munmap(buf, pagesize);

	/* huge page */
	buf = mmap(NULL, 2*1024*1024, PROT_READ|PROT_WRITE,
		   MAP_PRIVATE | MAP_HUGETLB | MAP_HUGE_2MB | MAP_ANONYMOUS,
		   -1, 0);
	if (buf == MAP_FAILED) {
		printf("Unable to map a huge page.  Try increasing "
		       "/proc/sys/vm/nr_hugepages by at least 1.\n");
		printf("Skipping the hugepage test\n");
	} else {
		/*
		 * This should succeed, so long as RLIMIT_MEMLOCK is
		 * not exceeded
		 */
		iov.iov_base = buf;
		iov.iov_len = 2*1024*1024;
		ret = io_uring_register(fd, IORING_REGISTER_BUFFERS, &iov, 1);
		if (ret < 0) {
			if (errno == ENOMEM)
				printf("Unable to test registering of a huge "
				       "page.  Try increasing the "
				       "RLIMIT_MEMLOCK resource limit by at "
				       "least 2MB.");
			else {
				printf("expected success, got %d\n", errno);
				status = 1;
			}
		} else {
			printf("Success!\n");
			ret = io_uring_register(fd, IORING_UNREGISTER_BUFFERS,
						0, 0);
			if (ret < 0) {
				perror("io_uring_unregister");
				status = 1;
			}
		}
	}
	ret = munmap(iov.iov_base, iov.iov_len);
	assert(ret == 0);

	/* file-backed buffers -- not supported */
	buf = map_filebacked(2*1024*1024);
	if (!buf)
		status = 1;
	iov.iov_base = buf;
	iov.iov_len = 2*1024*1024;
	printf("reserve file-backed buffers\n");
	status |= expect_fail(fd, IORING_REGISTER_BUFFERS, &iov, 1, EOPNOTSUPP);
	munmap(buf, 2*1024*1024);

	/* bump up against the soft limit and make sure we get EFAULT
	 * or whatever we're supposed to get.  NOTE: this requires
	 * running the test as non-root. */
	if (getuid() != 0)
		status |= test_memlock_exceeded(fd);

	return status;
}

void
dump_sqe(struct io_uring_sqe *sqe)
{
	printf("\topcode: %d\n", sqe->opcode);
	printf("\tflags:  0x%.8x\n", sqe->flags);
	printf("\tfd:     %d\n", sqe->fd);
	if (sqe->opcode == IORING_OP_POLL_ADD)
		printf("\tpoll_events: 0x%.8x\n", sqe->poll_events);
}

int
ioring_poll(struct io_uring *ring, int fd, int fixed)
{
	int ret;
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;

	sqe = io_uring_get_sqe(ring);
	memset(sqe, 0, sizeof(*sqe));
	sqe->opcode = IORING_OP_POLL_ADD;
	if (fixed)
		sqe->flags = IOSQE_FIXED_FILE;
	sqe->fd = fd;
	sqe->poll_events = POLLIN|POLLOUT;

	printf("io_uring_submit:\n");
	dump_sqe(sqe);
	ret = io_uring_submit(ring);
	if (ret != 1) {
		printf("failed to submit poll sqe: %d.\n", errno);
		return 1;
	}

	ret = io_uring_wait_cqe(ring, &cqe);
	if (ret < 0) {
		printf("io_uring_wait_cqe failed with %d\n", ret);
		return 1;
	}
	ret = 0;
	if (cqe->res != POLLOUT) {
		printf("io_uring_wait_cqe: expected 0x%.8x, got 0x%.8x\n",
		       POLLOUT, cqe->res);
		ret = 1;
	}

	io_uring_cqe_seen(ring, cqe);
	return ret;
}

int
test_poll_ringfd(void)
{
	int status = 0;
	int ret;
	int fd;
	struct io_uring ring;

	ret = io_uring_queue_init(1, &ring, 0);
	if (ret) {
		perror("io_uring_queue_init");
		return 1;
	}
	fd = ring.ring_fd;

	/* try polling the ring fd */
	status = ioring_poll(&ring, fd, 0);

	/*
	 * now register the ring fd, and try the poll again.  This should
	 * fail, because the kernel does not allow registering of the
	 * ring_fd.
	 */
	status |= expect_fail(fd, IORING_REGISTER_FILES, &fd, 1, EBADF);

	/* tear down queue */
	io_uring_queue_exit(&ring);

	return status;
}

int
main(int argc, char **argv)
{
	int fd, ret;
	unsigned int status = 0;
	struct io_uring_params p;
	struct rlimit rlim;

	/* setup globals */
	pagesize = getpagesize();
	ret = getrlimit(RLIMIT_MEMLOCK, &rlim);
	if (ret < 0) {
		perror("getrlimit");
		return 1;
	}
	mlock_limit = rlim.rlim_cur;
	printf("RELIMIT_MEMLOCK: %lu (%lu)\n", rlim.rlim_cur, rlim.rlim_max);
	devnull = open("/dev/null", O_RDWR);
	if (devnull < 0) {
		perror("open /dev/null");
		exit(1);
	}

	/* invalid fd */
	status |= expect_fail(-1, 0, NULL, 0, EBADF);
	/* valid fd that is not an io_uring fd */
	status |= expect_fail(devnull, 0, NULL, 0, EOPNOTSUPP);

	/* invalid opcode */
	memset(&p, 0, sizeof(p));
	fd = new_io_uring(1, &p);
	ret = expect_fail(fd, ~0U, NULL, 0, EINVAL);
	if (ret) {
		/* if this succeeds, tear down the io_uring instance
		 * and start clean for the next test. */
		close(fd);
		fd = new_io_uring(1, &p);
	}

	/* IORING_REGISTER_BUFFERS */
	status |= test_iovec_size(fd);
	status |= test_iovec_nr(fd);
	/* IORING_REGISTER_FILES */
	status |= test_max_fds(fd);
	close(fd);
	/* uring poll on the uring fd */
	status |= test_poll_ringfd();

	if (!status)
		printf("PASS\n");
	else
		printf("FAIL\n");

	return status;
}
