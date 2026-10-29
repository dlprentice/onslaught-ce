// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/save_replace.h (patch 0117): all-or-nothing replacement of one file in the port's writable folder.
// Careers (savegames\NAME.bes) and the options file (defaultoptions.bea) are written whole from a buffer. The retail PC
// code opened the file for writing, which cut it to nothing first, so a failed or interrupted save lost the old career.
// PortReplaceUserFile instead writes the complete bytes to a new file beside the target, flushes it to disk, reads it
// back and compares it with the buffer, and only then renames it over the target. Any failure leaves the previous file
// untouched and removes only the temporary file this call made. The player's game folder is never written: a target that
// exists only there becomes a new copy in the writable folder, which shadows the original.
#ifndef PORT_SAVE_REPLACE_H
#define PORT_SAVE_REPLACE_H

#ifdef __cplusplus
extern "C" {
#endif

// 0 on success. Otherwise the step that failed; the previous file is unchanged for every value except
// PORT_REPLACE_DURABLE (the Linux directory flush after a verified rename: the new file is in place and complete, but its
// directory entry is not known to be on disk, so the caller is told to report failure and the player may save again).
enum PortReplaceResult
{
	PORT_REPLACE_OK = 0,
	PORT_REPLACE_REFUSED,		// the target path or an existing target cannot be written (link, special file, folder, ambiguity)
	PORT_REPLACE_CREATE,		// the temporary file could not be created
	PORT_REPLACE_WRITE,			// writing the bytes failed
	PORT_REPLACE_SYNC,			// flushing the temporary file to disk failed
	PORT_REPLACE_VERIFY,		// the bytes read back differ from the buffer
	PORT_REPLACE_PUBLISH,		// the rename over the target failed
	PORT_REPLACE_DURABLE		// renamed, but the folder could not be flushed (Linux)
};

// game_path is the game's relative path ("savegames\\NAME.bes", "defaultoptions.bea"), resolved as every write is. `what`
// ("career", "options") only labels the one log line a failure prints; the line never names the file.
int PortReplaceUserFile(const char *game_path, const void *data, unsigned size, const char *what);

#ifdef _WIN32
// Windows only (files.cpp, used by save_replace.cpp): the UTF-8 path in the writable folder that a write to game_path reaches.
unsigned PortResolveUserPath(const char *game_path, char *out, unsigned n);
#endif

#ifdef __cplusplus
}
#endif

#endif
