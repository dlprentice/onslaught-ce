// SPDX-License-Identifier: GPL-3.0-only
// port/platform/linux/save_replace.cpp (patch 0117): all-or-nothing replacement of one file in the port's writable folder
// (port/platform/common/save_replace.h). The new bytes go to a hidden file in the same folder, are flushed to disk and read
// back, and then rename(2) puts them over the target in one step; the folder is flushed last. The previous file is never
// opened for writing, so no failure or interruption can leave it short or empty. A crash between the write and the rename
// leaves one hidden ".ce-write-" file, which is never listed as a career (careers are the *.bes entries).
// Two saves of one career at the same moment: the later rename wins, and a save that meets the other's rename while it checks the
// target waits for it (LOOKS below). That rename keeps the old file at no link for as long as it takes: about a microsecond on tmpfs,
// 40 to 400 microseconds on btrfs, and longer when the other process is descheduled inside it. A save that still finds the name
// without a link after all its looks (25.55 ms) is refused, with the career intact and nothing lost, as before this was added.
#include "handles.h"
#include "../common/portlog.h"
#include "../common/save_replace.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
// How often the target's name is looked up when the file it leads to has just lost its last name (another save's rename), and the
// wait before the second look, which doubles before each later one: 50, 100 ... 12,800 microseconds, 25.55 ms in all and none after
// the last look. Looking again at once does not work: 8 looks cost about 5 microseconds, and on btrfs the other rename takes longer.
const int LOOKS = 10;
const useconds_t FIRST_WAIT_US = 50;

const char *Reason(int step)
{
	switch (step)
	{
	case PORT_REPLACE_REFUSED: return "the file cannot be written";
	case PORT_REPLACE_CREATE: return "no temporary file";
	case PORT_REPLACE_WRITE: return "writing failed";
	case PORT_REPLACE_SYNC: return "flushing to disk failed";
	case PORT_REPLACE_VERIFY: return "the bytes read back differ";
	case PORT_REPLACE_PUBLISH: return "the rename failed";
	case PORT_REPLACE_DURABLE: return "the folder could not be flushed";
	}
	return "failed";
}

int Fail(int step, int error, const char *what)
{
	fprintf(stderr, PORT_LOG_PREFIX "%s was not saved: %s (%s); %s\n", what ? what : "file", Reason(step), strerror(error),
			step == PORT_REPLACE_DURABLE ? "the new file is complete, but save again to be sure it is on disk" : "the previous file is unchanged");
	errno = error;
	return step;
}

// The bytes of fd, read through the file system again (cached pages dropped first, advisory), equal the buffer.
bool ReadsBack(int fd, const unsigned char *bytes, unsigned size)
{
	struct stat st;
	if (fstat(fd, &st) != 0 || st.st_size != (off_t)size)
		return false;
	posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
	unsigned char back[65536];
	for (unsigned at = 0; at < size;)
	{
		unsigned want = size - at < sizeof(back) ? size - at : (unsigned)sizeof(back);
		ssize_t got = pread(fd, back, want, at);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0 || memcmp(back, bytes + at, got) != 0)
			return false;
		at += got;
	}
	return true;
}
}

int PortReplaceUserFile(const char *game_path, const void *data, unsigned size, const char *what)
{
	const unsigned char *bytes = (const unsigned char *)data;
	// A career or an options file is never empty: replacing one with nothing could only be a caller's mistake. (Retail's
	// fwrite of zero bytes reported failure too.)
	if (!data || !size)
		return Fail(PORT_REPLACE_REFUSED, EINVAL, what);
	char leaf[256];
	int parent = PortUserParent(game_path, leaf, sizeof(leaf), true);
	if (parent < 0)
		return Fail(PORT_REPLACE_REFUSED, errno, what);

	// An existing target must be a plain file with one name: a link would be replaced instead of written through, a
	// second hard link would keep the old bytes alive, and a special file or folder is not a career.
	// Another save of this career can rename over the name between our lookup and our reading the attributes: the file we
	// found has then lost its only name (st_nlink 0), and the name leads to it until that rename returns, after which it holds
	// that save's complete file. Wait for the rename and look the name up again instead of refusing; a file that still shows no
	// link after all the looks is not a career we can replace. Only this path sleeps: an ordinary save never waits.
	struct stat target;
	bool exists = false;
	for (int look = 0; look < LOOKS; look++)
	{
		exists = fstatat(parent, leaf, &target, AT_SYMLINK_NOFOLLOW) == 0;
		if (!exists || target.st_nlink != 0)
			break;
		if (look + 1 < LOOKS)
			usleep(FIRST_WAIT_US << look);
	}
	if (exists ? (!S_ISREG(target.st_mode) || target.st_nlink != 1) : errno != ENOENT)
	{
		int error = exists ? EACCES : errno;
		close(parent);
		return Fail(PORT_REPLACE_REFUSED, error, what);
	}

	// Opening a career for writing used to fail when its file was read-only; a rename ignores the target's own permissions,
	// so keep that protection: a career this user could not write is not replaced.
	if (exists && faccessat(parent, leaf, W_OK, AT_EACCESS) != 0)
	{
		int error = errno;
		close(parent);
		return Fail(PORT_REPLACE_REFUSED, error, what);
	}

	static unsigned sequence;
	char temp[80];
	int fd = -1;
	for (int tries = 0; tries < 16 && fd < 0; tries++)
	{
		snprintf(temp, sizeof(temp), ".ce-write-%ld-%u", (long)getpid(), __sync_add_and_fetch(&sequence, 1));
		fd = openat(parent, temp, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
		if (fd < 0 && errno != EEXIST)
			break;
	}
	struct stat mine;
	if (fd >= 0 && fstat(fd, &mine) != 0)
	{
		int error = errno;
		// Unowned until its identity is known: leave it rather than remove a name that may not be ours.
		close(fd);
		close(parent);
		return Fail(PORT_REPLACE_CREATE, error, what);
	}
	if (fd < 0)
	{
		int error = errno;
		close(parent);
		return Fail(PORT_REPLACE_CREATE, error, what);
	}
	if (exists)
		fchmod(fd, target.st_mode & 07777);		// the new file keeps the old one's permissions

	int step = PORT_REPLACE_OK, error = 0;
	for (unsigned done = 0; done < size && !step;)
	{
		ssize_t put = write(fd, bytes + done, size - done);
		if (put < 0 && errno == EINTR)
			continue;
		if (put <= 0)
			step = PORT_REPLACE_WRITE, error = put < 0 ? errno : EIO;
		else
			done += put;
	}
	if (!step && fsync(fd) != 0)
		step = PORT_REPLACE_SYNC, error = errno;
	if (!step && !ReadsBack(fd, bytes, size))
		step = PORT_REPLACE_VERIFY, error = EIO;
	if (!step)
	{
		// The name must still be the file just verified; then the descriptor is closed, so a deferred write error reaches us.
		struct stat named;
		if (fstatat(parent, temp, &named, AT_SYMLINK_NOFOLLOW) != 0 || named.st_dev != mine.st_dev || named.st_ino != mine.st_ino ||
			named.st_nlink != 1)
			step = PORT_REPLACE_PUBLISH, error = EBUSY;
	}
	int closed = close(fd);
	if (!step && closed != 0)
		step = PORT_REPLACE_WRITE, error = errno;
	if (!step && renameat(parent, temp, parent, leaf) != 0)
		step = PORT_REPLACE_PUBLISH, error = errno;

	if (step)
	{
		// Remove the temporary file, and only that one: it is ours if its name still holds the inode created above.
		struct stat named;
		if (fstatat(parent, temp, &named, AT_SYMLINK_NOFOLLOW) == 0 && named.st_dev == mine.st_dev && named.st_ino == mine.st_ino &&
			unlinkat(parent, temp, 0) != 0)
			fprintf(stderr, PORT_LOG_PREFIX "a temporary file could not be removed from the writable folder\n");
		close(parent);
		return Fail(step, error, what);
	}

	// The rename is only known to survive a power cut once the folder is flushed. O_PATH descriptors cannot be flushed.
	int folder = openat(parent, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (folder < 0 || (fsync(folder) != 0 && errno != EINVAL && errno != ENOTSUP && errno != ENOSYS))
		step = PORT_REPLACE_DURABLE, error = errno;
	if (folder >= 0)
		close(folder);
	close(parent);
	return step ? Fail(step, error, what) : PORT_REPLACE_OK;
}
