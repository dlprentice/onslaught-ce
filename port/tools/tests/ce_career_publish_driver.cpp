// SPDX-License-Identifier: GPL-3.0-only
// Host & Windows/Wine driver exercising port/platform/common/ce_career_publish.h against
// the real Linux and Windows publish backends and real preview readers.
#include "../../platform/common/ce_career_publish.h"
#include "../../platform/common/ce_backup_preview.h"
#include "../../platform/common/ce_career_restore.h"
#include "../../game/ce_career_view.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static std::string				gBase;
static bool						gPermit = true;
static bool						gNamePermit = true;
static int						gPermitCalls = 0;
static int						gNameCalls = 0;
static int						gFails = 0;
static const char				*gCase = "";
static SDL_DialogFileCallback	gSavedCallback = nullptr;
static void						*gSavedUserdata = nullptr;

#define EXPECT(cond)                                                               \
	do                                                                             \
	{                                                                              \
		if (!(cond))                                                               \
		{                                                                          \
			std::printf("FAIL %s line %d: %s\n", gCase, __LINE__, #cond);          \
			++gFails;                                                              \
		}                                                                          \
	} while (0)

static bool	permitCb(void *)
{
	++gPermitCalls;
	return gPermit;
}

static bool	nameCb(void *, const char *, const char *)
{
	++gNameCalls;
	return gNamePermit;
}

static bool	readFile(const std::string &path, std::vector<unsigned char> &out)
{
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	out.resize(CE_CAREER_SIZE);
	const size_t n = std::fread(out.data(), 1, out.size(), f);
	std::fclose(f);
	return n == CE_CAREER_SIZE;
}

#ifndef _WIN32

#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static bool			gFailSavesFsync = false;
static bool			gInjectCaseAliasOnFsync = false;
static bool			gSwapBackupOnFsync = false;
static dev_t		gSavesDev = 0;
static ino_t		gSavesIno = 0;
static std::string	gSavesDir;
static std::string	gBackupDir;
static std::vector<unsigned char> gAliasPayload;

static bool	copyDirContents(const std::string &srcDir, const std::string &dstDir)
{
	if (mkdir(dstDir.c_str(), 0700) != 0)
		return false;
	DIR *d = opendir(srcDir.c_str());
	if (!d)
		return false;
	bool ok = true;
	while (dirent *ent = readdir(d))
	{
		const std::string name = ent->d_name;
		if (name == "." || name == "..")
			continue;
		FILE *in = std::fopen((srcDir + "/" + name).c_str(), "rb");
		FILE *out = std::fopen((dstDir + "/" + name).c_str(), "wb");
		if (!in || !out)
		{
			if (in)
				std::fclose(in);
			if (out)
				std::fclose(out);
			ok = false;
			break;
		}
		unsigned char buf[4096];
		size_t n = 0;
		while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0)
		{
			if (std::fwrite(buf, 1, n, out) != n)
			{
				ok = false;
				break;
			}
		}
		std::fclose(in);
		std::fclose(out);
		if (!ok)
			break;
	}
	closedir(d);
	return ok;
}

extern "C"
{
const char	*SDL_GetBasePath(void)
{
	static std::string pkg;
	pkg = gBase + "/package/";
	return pkg.c_str();
}

SDL_Window	*SDL_GetKeyboardFocus(void)
{
	return nullptr;
}

void	SDL_ShowOpenFileDialog(SDL_DialogFileCallback callback, void *userdata, SDL_Window *,
							   const SDL_DialogFileFilter *, int, const char *, bool)
{
	gSavedCallback = callback;
	gSavedUserdata = userdata;
}

int	__real_fsync(int fd);
int	__wrap_fsync(int fd)
{
	struct stat st{};
	if (fstat(fd, &st) == 0 && st.st_dev == gSavesDev && st.st_ino == gSavesIno)
	{
		if (gFailSavesFsync)
		{
			gFailSavesFsync = false;
			errno = EIO;
			return -1;
		}
		if (gInjectCaseAliasOnFsync)
		{
			gInjectCaseAliasOnFsync = false;
			FILE *f = std::fopen((gSavesDir + "/hero.bes").c_str(), "wb");
			if (f)
			{
				std::fwrite(gAliasPayload.data(), 1, gAliasPayload.size(), f);
				std::fclose(f);
			}
		}
		if (gSwapBackupOnFsync)
		{
			gSwapBackupOnFsync = false;
			DIR *d = opendir(gBackupDir.c_str());
			if (d)
			{
				std::string setName;
				while (dirent *ent = readdir(d))
				{
					const std::string n = ent->d_name;
					if (n.rfind("onslaught-backup-", 0) == 0)
					{
						setName = n;
						break;
					}
				}
				closedir(d);
				if (!setName.empty())
				{
					const std::string orig = gBackupDir + "/" + setName;
					const std::string old = gBackupDir + "/.old-" + setName;
					const std::string clone = gBackupDir + "/.clone-" + setName;
					if (copyDirContents(orig, clone))
					{
						rename(orig.c_str(), old.c_str());
						rename(clone.c_str(), orig.c_str());
					}
				}
			}
		}
	}
	return __real_fsync(fd);
}
}

int	main(int argc, char **argv)
{
	if (argc < 3)
		return 2;
	gBase = argv[1];
	const std::string mode = argv[2];
	gCase = argv[2];

	const std::string user = gBase + "/user";
	const std::string game = gBase + "/game";
	const std::string pkg = gBase + "/package";
	const std::string backup = gBase + "/backup";
	const std::string extFile = gBase + "/external/Hero.bes";

	if (mode == "import_ok")
	{
		unsigned char bytes[CE_CAREER_SIZE]{};
		char leaf[256]{}, err[192]{}, target[80]{};
		EXPECT(CeCareerImportRead(extFile.c_str(), user.c_str(), game.c_str(), pkg.c_str(),
								  bytes, sizeof(bytes), leaf, sizeof(leaf), err, sizeof(err)) == CE_IMPORT_READ_OK);
		EXPECT(!std::strcmp(leaf, "Hero.bes"));
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, user.c_str(), game.c_str(), leaf,
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "Hero.bes"));
		EXPECT(gNameCalls >= 1);

		CeCareerPublishRequest req{};
		req.userDir = user.c_str();
		req.gameDir = game.c_str();
		req.packageDir = pkg.c_str();
		req.backupRoot = backup.c_str();
		req.sourceName = leaf;
		req.targetName = target;
		req.bytes = bytes;
		req.size = CE_CAREER_SIZE;
		req.permitted = permitCb;
		req.namePermitted = nameCb;
		const CeCareerPublishResult res = CeCareerPublishNew(&req);
		EXPECT(res.status == CE_PUBLISH_COMPLETE);
		EXPECT(res.possible == 1);
		EXPECT(!std::strcmp(res.newFile, "Hero.bes"));
		EXPECT(res.backupSet[0] != 0);
		std::printf("target=%s\nbackup=%s\n", res.newFile, res.backupSet);
	}
	else if (mode == "import_collision_suggest")
	{
		char target[80]{};
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, user.c_str(), game.c_str(), "Existing.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "Existing imported.bes"));

		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, user.c_str(), game.c_str(), "Again.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "Again imported 2.bes"));

		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, user.c_str(), game.c_str(), "Again imported.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "Again imported 2.bes"));

		const char *longName = "VeryLongExternalCareerFilenameExceedingThirtyOneChars.bes";
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, user.c_str(), game.c_str(), longName,
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(std::strlen(target) <= 35 && std::strlen(target) > 4);

		// COM0 and LPT0 must be treated as reserved Windows device names on both platforms.
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, user.c_str(), game.c_str(), "COM0.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "Imported.bes"));
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, user.c_str(), game.c_str(), "LPT0.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "Imported.bes"));

		std::vector<unsigned char> payload;
		EXPECT(readFile(extFile, payload));
		for (const char *badTarget : {"COM0.bes", "LPT0.bes"})
		{
			CeCareerPublishRequest req{};
			req.userDir = user.c_str();
			req.gameDir = game.c_str();
			req.packageDir = pkg.c_str();
			req.backupRoot = backup.c_str();
			req.sourceName = "Hero.bes";
			req.targetName = badTarget;
			req.bytes = payload.data();
			req.size = payload.size();
			req.permitted = permitCb;
			req.namePermitted = nameCb;
			const CeCareerPublishResult res = CeCareerPublishNew(&req);
			EXPECT(res.status == CE_PUBLISH_BAD_NAME);
			EXPECT(res.possible == 0 && res.newFile[0] == 0);
		}
		std::printf("long=%s\n", target);
	}
	else if (mode == "edit_ok")
	{
		std::vector<unsigned char> edited;
		EXPECT(readFile(extFile, edited));
		char target[80]{};
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_EDIT, user.c_str(), game.c_str(), "Existing.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "Existing edited.bes"));

		char target2[80]{};
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_EDIT, user.c_str(), game.c_str(), "Again edited.bes",
									  nameCb, nullptr, target2, sizeof(target2)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target2, "Again edited 2.bes"));

		CeCareerPublishRequest req{};
		req.userDir = user.c_str();
		req.gameDir = game.c_str();
		req.packageDir = pkg.c_str();
		req.backupRoot = backup.c_str();
		req.sourceName = "Existing.bes";
		req.targetName = target;
		req.bytes = edited.data();
		req.size = edited.size();
		req.permitted = permitCb;
		req.namePermitted = nameCb;
		const CeCareerPublishResult res = CeCareerPublishNew(&req);
		EXPECT(res.status == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(res.newFile, "Existing edited.bes"));
		EXPECT(res.backupSet[0] != 0);
		std::printf("target=%s\nbackup=%s\n", res.newFile, res.backupSet);
	}
	else if (mode == "occupied_user" || mode == "occupied_game")
	{
		std::vector<unsigned char> payload;
		EXPECT(readFile(extFile, payload));
		const char *target = (mode == "occupied_user") ? "existing.bes" : "GameSlot.bes";
		CeCareerPublishRequest req{};
		req.userDir = user.c_str();
		req.gameDir = game.c_str();
		req.packageDir = pkg.c_str();
		req.backupRoot = backup.c_str();
		req.sourceName = "Hero.bes";
		req.targetName = target;
		req.bytes = payload.data();
		req.size = payload.size();
		req.permitted = permitCb;
		req.namePermitted = nameCb;
		const CeCareerPublishResult res = CeCareerPublishNew(&req);
		EXPECT(res.status == CE_PUBLISH_OCCUPIED);
		EXPECT(res.possible == 0 && res.newFile[0] == 0);
	}
	else if (mode == "no_backup_folder" || mode == "backup_inside_user" ||
			 mode == "backup_busy" || mode == "backup_no_existing_careers" ||
			 mode == "authority_refused" || mode == "name_refused")
	{
		std::vector<unsigned char> payload;
		EXPECT(readFile(extFile, payload));
		int lockFd = -1;
		if (mode == "backup_busy")
		{
			const std::string lockPath = user + "/.ce-career-custody.lock";
			lockFd = ::open(lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
			EXPECT(lockFd >= 0 && flock(lockFd, LOCK_EX | LOCK_NB) == 0);
		}
		if (mode == "authority_refused")
			gPermit = false;
		if (mode == "name_refused")
			gNamePermit = false;

		const std::string insideBackup = user + "/subbackup";
		CeCareerPublishRequest req{};
		req.userDir = user.c_str();
		req.gameDir = game.c_str();
		req.packageDir = pkg.c_str();
		req.backupRoot = (mode == "no_backup_folder") ? ""
					   : (mode == "backup_inside_user") ? insideBackup.c_str()
														: backup.c_str();
		req.sourceName = "Hero.bes";
		req.targetName = "Fresh.bes";
		req.bytes = payload.data();
		req.size = payload.size();
		req.permitted = permitCb;
		req.namePermitted = nameCb;
		const CeCareerPublishResult res = CeCareerPublishNew(&req);
		if (mode == "no_backup_folder")
			EXPECT(res.status == CE_PUBLISH_NO_FOLDER);
		else if (mode == "backup_inside_user")
			EXPECT(res.status == CE_PUBLISH_REFUSED);
		else if (mode == "backup_no_existing_careers")
			EXPECT(res.status == CE_PUBLISH_COMPLETE);
		else if (mode == "backup_busy")
			EXPECT(res.status == CE_PUBLISH_BUSY);
		else if (mode == "authority_refused")
			EXPECT(res.status == CE_PUBLISH_AUTHORITY);
		else if (mode == "name_refused")
			EXPECT(res.status == CE_PUBLISH_BAD_NAME);
		if (mode == "backup_no_existing_careers")
			EXPECT(res.possible == 1 && !std::strcmp(res.newFile, "Fresh.bes") && res.backupSet[0] == 0);
		else
			EXPECT(res.possible == 0 && res.newFile[0] == 0);
		if (lockFd >= 0)
			close(lockFd);
	}
	else if (mode == "read_refusals")
	{
		unsigned char bytes[CE_CAREER_SIZE]{};
		char leaf[256]{}, err[192]{};
		auto checkRead = [&](const std::string &path)
		{
			return CeCareerImportRead(path.c_str(), user.c_str(), game.c_str(), pkg.c_str(),
									  bytes, sizeof(bytes), leaf, sizeof(leaf), err, sizeof(err));
		};
		EXPECT(checkRead(gBase + "/external/symlink.bes") == CE_IMPORT_READ_UNSAFE_FILE);
		EXPECT(checkRead(gBase + "/ext_link/Hero.bes") == CE_IMPORT_READ_BAD_PATH);
		EXPECT(checkRead(gBase + "/external/hardlink.bes") == CE_IMPORT_READ_UNSAFE_FILE);
		EXPECT(checkRead(user + "/savegames/Existing.bes") == CE_IMPORT_READ_BAD_PATH);
		EXPECT(checkRead(game + "/savegames/GameSlot.bes") == CE_IMPORT_READ_BAD_PATH);
		EXPECT(checkRead(pkg + "/Packaged.bes") == CE_IMPORT_READ_BAD_PATH);
		EXPECT(checkRead(gBase + "/external/short.bes") == CE_IMPORT_READ_BAD_SIZE);
		EXPECT(checkRead(gBase + "/external/long.bes") == CE_IMPORT_READ_BAD_SIZE);
		EXPECT(checkRead(gBase + "/external/empty.bes") == CE_IMPORT_READ_BAD_SIZE);
		EXPECT(checkRead(gBase + "/external/badver.bes") == CE_IMPORT_READ_BAD_CAREER);
		EXPECT(checkRead(gBase + "/external/implausible.bes") == CE_IMPORT_READ_BAD_CAREER);
		EXPECT(checkRead(gBase + "/external/badgoodie.bes") == CE_IMPORT_READ_BAD_CAREER);
		EXPECT(checkRead(gBase + "/external/bidi\xe2\x80\xae.bes") == CE_IMPORT_READ_BAD_PATH);
	}
	else if (mode == "picker_lifecycle")
	{
		CeImportPickStatus st{};
		CeCareerImportPickEnter();
		CeCareerImportPickPoll(&st);
		EXPECT(st.state == CE_IMPORT_PICK_IDLE);

		CeCareerImportPickOpen();
		CeCareerImportPickPoll(&st);
		EXPECT(st.state == CE_IMPORT_PICK_OPEN && gSavedCallback != nullptr);
		SDL_DialogFileCallback staleCb = gSavedCallback;
		void *staleData = gSavedUserdata;
		CeCareerImportPickLeave();
		const char *validList[] = {extFile.c_str(), nullptr};
		staleCb(staleData, validList, 0);
		CeCareerImportPickPoll(&st);
		EXPECT(st.state == CE_IMPORT_PICK_IDLE && st.path[0] == 0);

		CeCareerImportPickEnter();
		CeCareerImportPickOpen();
		staleCb = gSavedCallback;
		staleData = gSavedUserdata;
		CeCareerImportPickEnter();
		staleCb(staleData, validList, 0);
		CeCareerImportPickPoll(&st);
		EXPECT(st.state == CE_IMPORT_PICK_IDLE && st.path[0] == 0);

		CeCareerImportPickOpen();
		const char *emptyList[] = {nullptr};
		gSavedCallback(gSavedUserdata, emptyList, -1);
		CeCareerImportPickPoll(&st);
		EXPECT(st.state == CE_IMPORT_PICK_CANCELLED);

		CeCareerImportPickOpen();
		gSavedCallback(gSavedUserdata, nullptr, -1);
		CeCareerImportPickPoll(&st);
		EXPECT(st.state == CE_IMPORT_PICK_ERROR);

		CeCareerImportPickOpen();
		gSavedCallback(gSavedUserdata, validList, 0);
		CeCareerImportPickPoll(&st);
		EXPECT(st.state == CE_IMPORT_PICK_READY);
		EXPECT(!std::strcmp(st.path, extFile.c_str()));
		EXPECT(!std::strcmp(st.leaf, "Hero.bes"));
		CeCareerImportPickLeave();
	}
	else if (mode == "uncertain_after_link" || mode == "post_link_case_alias" ||
			 mode == "post_link_backup_swapped")
	{
		std::vector<unsigned char> payload;
		EXPECT(readFile(extFile, payload));
		struct stat st{};
		gSavesDir = user + "/savegames";
		gBackupDir = backup;
		EXPECT(stat(gSavesDir.c_str(), &st) == 0);
		gSavesDev = st.st_dev;
		gSavesIno = st.st_ino;
		if (mode == "uncertain_after_link")
			gFailSavesFsync = true;
		else if (mode == "post_link_case_alias")
		{
			gAliasPayload = payload;
			gInjectCaseAliasOnFsync = true;
		}
		else if (mode == "post_link_backup_swapped")
			gSwapBackupOnFsync = true;

		CeCareerPublishRequest req{};
		req.userDir = user.c_str();
		req.gameDir = game.c_str();
		req.packageDir = pkg.c_str();
		req.backupRoot = backup.c_str();
		req.sourceName = "Hero.bes";
		req.targetName = "Hero.bes";
		req.bytes = payload.data();
		req.size = payload.size();
		req.permitted = permitCb;
		req.namePermitted = nameCb;
		const CeCareerPublishResult res = CeCareerPublishNew(&req);
		EXPECT(res.status == CE_PUBLISH_UNCERTAIN);
		EXPECT(res.possible == 1);
		EXPECT(!std::strcmp(res.newFile, "Hero.bes"));
		EXPECT(res.backupSet[0] != 0);
		std::printf("target=%s\nbackup=%s\n", res.newFile, res.backupSet);
	}
	else if (mode == "absent_savegames_dirs")
	{
		std::vector<unsigned char> payload;
		EXPECT(readFile(extFile, payload));
		char target[80]{};
		auto makeReq = [&](const std::string &u, const std::string &g, const std::string &p, const std::string &b) {
			CeCareerPublishRequest req{};
			req.userDir = u.c_str();
			req.gameDir = g.c_str();
			req.packageDir = p.c_str();
			req.backupRoot = b.c_str();
			req.sourceName = "Hero.bes";
			req.targetName = "Hero.bes";
			req.bytes = payload.data();
			req.size = payload.size();
			req.permitted = permitCb;
			req.namePermitted = nameCb;
			return req;
		};

		// 1) game/savegames absent, user/savegames populated: suggest + publish succeed without creating game/savegames.
		const std::string g1 = gBase + "/no_game_saves/game";
		const std::string u1 = gBase + "/no_game_saves/user";
		const std::string p1 = gBase + "/no_game_saves/package";
		const std::string b1 = gBase + "/no_game_saves/backup";
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, u1.c_str(), g1.c_str(), "Existing.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "Existing imported.bes"));
		CeCareerPublishRequest req1 = makeReq(u1, g1, p1, b1);
		const CeCareerPublishResult res1 = CeCareerPublishNew(&req1);
		EXPECT(res1.status == CE_PUBLISH_COMPLETE);
		struct stat st{};
		EXPECT(lstat((g1 + "/savegames").c_str(), &st) != 0 && errno == ENOENT);
		EXPECT(lstat((u1 + "/savegames/Hero.bes").c_str(), &st) == 0 && S_ISREG(st.st_mode));

		// 2) user/savegames absent, game/savegames populated: suggest & failed gates MUST NOT create user/savegames;
		//    authorized publish creates user/savegames after backup + gates pass.
		const std::string g2 = gBase + "/no_user_saves/game";
		const std::string u2 = gBase + "/no_user_saves/user";
		const std::string p2 = gBase + "/no_user_saves/package";
		const std::string b2 = gBase + "/no_user_saves/backup";
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, u2.c_str(), g2.c_str(), "GameSlot.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "GameSlot imported.bes"));
		EXPECT(lstat((u2 + "/savegames").c_str(), &st) != 0 && errno == ENOENT);

		CeCareerPublishRequest reqAuth = makeReq(u2, g2, p2, b2);
		gPermit = false;
		EXPECT(CeCareerPublishNew(&reqAuth).status == CE_PUBLISH_AUTHORITY);
		EXPECT(lstat((u2 + "/savegames").c_str(), &st) != 0 && errno == ENOENT);
		gPermit = true;

		gNamePermit = false;
		EXPECT(CeCareerPublishNew(&reqAuth).status == CE_PUBLISH_BAD_NAME);
		EXPECT(lstat((u2 + "/savegames").c_str(), &st) != 0 && errno == ENOENT);
		gNamePermit = true;

		CeCareerPublishRequest reqNoFolder = makeReq(u2, g2, p2, "");
		EXPECT(CeCareerPublishNew(&reqNoFolder).status == CE_PUBLISH_NO_FOLDER);
		EXPECT(lstat((u2 + "/savegames").c_str(), &st) != 0 && errno == ENOENT);

		const CeCareerPublishResult res2 = CeCareerPublishNew(&reqAuth);
		EXPECT(res2.status == CE_PUBLISH_COMPLETE);
		EXPECT(lstat((u2 + "/savegames/Hero.bes").c_str(), &st) == 0 && S_ISREG(st.st_mode));

		// 3) Both user/savegames and game/savegames absent (zero existing files): 0-career import succeeds cleanly (05-P0-1).
		const std::string g3 = gBase + "/both_absent/game";
		const std::string u3 = gBase + "/both_absent/user";
		const std::string p3 = gBase + "/both_absent/package";
		const std::string b3 = gBase + "/both_absent/backup";
		CeCareerPublishRequest req3 = makeReq(u3, g3, p3, b3);
		EXPECT(CeCareerPublishNew(&req3).status == CE_PUBLISH_COMPLETE);
		EXPECT(lstat((u3 + "/savegames/Hero.bes").c_str(), &st) == 0 && S_ISREG(st.st_mode));

		// 4) Non-directory (regular file or symlink) at savegames is strictly refused.
		const std::string g4 = gBase + "/file_at_saves/game";
		const std::string u4 = gBase + "/file_at_saves/user";
		const std::string p4 = gBase + "/file_at_saves/package";
		const std::string b4 = gBase + "/file_at_saves/backup";
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, u4.c_str(), g4.c_str(), "Hero.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_REFUSED);
		CeCareerPublishRequest req4 = makeReq(u4, g4, p4, b4);
		EXPECT(CeCareerPublishNew(&req4).status == CE_PUBLISH_REFUSED);
	}
	else
		return 2;

	std::printf("RESULT %s\n", gFails ? "fail" : "pass");
	return gFails ? 1 : 0;
}

#else // _WIN32

#include "../../platform/windows/ce_backup_paths.h"
#include "../../platform/common/ce_backup_verify.h"

static int						gTakeFullSetCalls = 0;
static bool						gFailBackup = false;
static bool						gHoldLockAfterBackup = false;
static HANDLE					gHeldLockHandle = INVALID_HANDLE_VALUE;
static bool						gRaceCreateTarget = false;
static std::vector<unsigned char> gRaceBytes;

static std::string	winJoin(const std::string &a, const std::string &b)
{
	if (a.empty())
		return b;
	if (a.back() == '\\' || a.back() == '/')
		return a + b;
	return a + "\\" + b;
}

static std::string	jsonEscape(const std::string &s)
{
	std::string out = "\"";
	for (char c : s)
	{
		if (c == '\\' || c == '"')
			out.push_back('\\');
		out.push_back(c);
	}
	out.push_back('"');
	return out;
}

static bool	writeWholeWinFile(const std::wstring &path, const void *data, size_t size)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
						   FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	DWORD put = 0;
	const bool ok = WriteFile(h, data, static_cast<DWORD>(size), &put, nullptr) && put == size;
	FlushFileBuffers(h);
	CloseHandle(h);
	return ok;
}

static bool	computeTestStamp(const std::string &setPath, const std::string &expectedIdentity,
							 const std::string &user, const std::string &game,
							 const std::string &pkg, std::string &out)
{
	out.clear();
	CePreviewSet set;
	std::string err;
	if (!CePreviewRead(setPath, user, game, pkg, set, err) || set.identity != expectedIdentity)
		return false;
	std::wstring wSet;
	if (!ce_backup_paths::wide(setPath.c_str(), wSet))
		return false;
	HANDLE dir = CreateFileW(wSet.c_str(), FILE_READ_ATTRIBUTES,
							 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
							 nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (dir == INVALID_HANDLE_VALUE)
		return false;
	FILE_ID_INFO id{};
	const bool got = GetFileInformationByHandleEx(dir, FileIdInfo, &id, sizeof(id)) != FALSE;
	CloseHandle(dir);
	if (!got)
		return false;
	std::string rec = std::to_string(id.VolumeSerialNumber) + ":";
	for (unsigned char b : id.FileId.Identifier)
	{
		char hex[4];
		std::snprintf(hex, sizeof(hex), "%02x", unsigned(b));
		rec += hex;
	}
	rec += ":" + set.identity;
	out = ce_backup_verify::digest(rec);
	return true;
}

// Lightweight test stubs for CeCareerTakeFullSet and CeCareerRestoreStamp so the Windows
// publish backend is tested independently without modifying windows/ce_career_restore.cpp.
CeCareerStorageResult	CeCareerTakeFullSet(const CeCareerStorageRequest &request)
{
	++gTakeFullSetCalls;
	CeCareerStorageResult res;
	if (gFailBackup)
	{
		res.status = CeCareerStorageStatus::Refused;
		return res;
	}
	const std::string setPath = winJoin(request.backupRoot, "onslaught-backup-20261121-120000");
	std::wstring wSet;
	if (!ce_backup_paths::wide(setPath.c_str(), wSet) || !CreateDirectoryW(wSet.c_str(), nullptr))
	{
		res.status = CeCareerStorageStatus::Refused;
		return res;
	}
	std::string srcLeaf = "Existing.bes";
	std::string srcFile = winJoin(winJoin(request.user, "savegames"), srcLeaf);
	std::vector<unsigned char> data;
	if (!readFile(srcFile, data))
	{
		srcLeaf = "GameSlot.bes";
		srcFile = winJoin(winJoin(request.game, "savegames"), srcLeaf);
		if (!readFile(srcFile, data))
		{
			res.status = CeCareerStorageStatus::Refused;
			return res;
		}
	}
	const std::string dstFile = winJoin(setPath, srcLeaf);
	std::wstring wDst;
	if (!ce_backup_paths::wide(dstFile.c_str(), wDst) ||
		!writeWholeWinFile(wDst, data.data(), data.size()))
	{
		res.status = CeCareerStorageStatus::Refused;
		return res;
	}
	const std::string bytesStr(reinterpret_cast<const char *>(data.data()), data.size());
	const std::string json =
		std::string("{\"Folder\":") + jsonEscape(request.user) +
		",\"Game\":" + jsonEscape(request.game) +
		",\"Created\":\"2026-11-21T12:00:00Z\",\"Reason\":\"All careers\",\"Target\":1,\"Files\":[{" +
		"\"Name\":" + jsonEscape(srcLeaf) + ",\"Original\":" + jsonEscape(srcFile) +
		",\"Size\":" + std::to_string(data.size()) +
		",\"Sha256\":" + jsonEscape(ce_backup_verify::digest(bytesStr)) + "}]}\n";
	const std::string manifestPath = winJoin(setPath, "onslaught-backup.json");
	std::wstring wManifest;
	if (!ce_backup_paths::wide(manifestPath.c_str(), wManifest) ||
		!writeWholeWinFile(wManifest, json.data(), json.size()))
	{
		res.status = CeCareerStorageStatus::Refused;
		return res;
	}

	CePreviewSet parsed;
	std::string err;
	if (!CePreviewRead(setPath, request.user, request.game, request.package, parsed, err))
	{
		res.status = CeCareerStorageStatus::Refused;
		return res;
	}
	std::string stamp;
	if (!computeTestStamp(setPath, parsed.identity, request.user, request.game, request.package, stamp))
	{
		res.status = CeCareerStorageStatus::Refused;
		return res;
	}

	if (gHoldLockAfterBackup)
	{
		const std::string lockPath = winJoin(request.user, ".ce-career-custody.lock");
		std::wstring wLock;
		if (ce_backup_paths::wide(lockPath.c_str(), wLock))
		{
			gHeldLockHandle = CreateFileW(wLock.c_str(), GENERIC_READ | GENERIC_WRITE,
										  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
										  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (gHeldLockHandle != INVALID_HANDLE_VALUE)
			{
				OVERLAPPED ov{};
				LockFileEx(gHeldLockHandle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
						   0, 1, 0, &ov);
			}
		}
	}

	res.status = CeCareerStorageStatus::Complete;
	res.backupSet = setPath;
	res.backupIdentity = parsed.identity;
	res.backupStamp = stamp;
	return res;
}

bool	CeCareerRestoreStamp(const CeCareerRestoreRequest &request, std::string &stampOut)
{
	return computeTestStamp(request.sourceSet, request.sourceIdentity,
							request.user, request.game, request.package, stampOut);
}

void	CePublishTestBeforeRename(HANDLE savesDir, const wchar_t *targetName)
{
	if (!gRaceCreateTarget || !targetName || gRaceBytes.empty())
		return;
	gRaceCreateTarget = false;
	std::wstring dirPath;
	if (!ce_backup_paths::physical(savesDir, dirPath))
		return;
	const std::wstring full = dirPath + L"\\" + targetName;
	writeWholeWinFile(full, gRaceBytes.data(), gRaceBytes.size());
}

extern "C"
{
const char	*SDL_GetBasePath(void)
{
	static std::string pkg;
	pkg = winJoin(gBase, "package") + "\\";
	return pkg.c_str();
}

SDL_Window	*SDL_GetKeyboardFocus(void)
{
	return nullptr;
}

void	SDL_ShowOpenFileDialog(SDL_DialogFileCallback callback, void *userdata, SDL_Window *,
							   const SDL_DialogFileFilter *, int, const char *, bool)
{
	gSavedCallback = callback;
	gSavedUserdata = userdata;
}
}

int	main(int argc, char **argv)
{
	(void)&ce_backup_paths::utf8;
	(void)&ce_backup_paths::physicalLeaf;
	if (argc < 3)
		return 2;
	gBase = argv[1];
	const std::string mode = argv[2];
	gCase = argv[2];

	const std::string user = winJoin(gBase, "user");
	const std::string game = winJoin(gBase, "game");
	const std::string pkg = winJoin(gBase, "package");
	const std::string backup = winJoin(gBase, "backup");
	const std::string extFile = winJoin(winJoin(gBase, "external"), "Hero.bes");

	if (mode == "import_ok")
	{
		unsigned char bytes[CE_CAREER_SIZE]{};
		char leaf[256]{}, err[192]{}, target[80]{};
		EXPECT(CeCareerImportRead(extFile.c_str(), user.c_str(), game.c_str(), pkg.c_str(),
								  bytes, sizeof(bytes), leaf, sizeof(leaf), err, sizeof(err)) == CE_IMPORT_READ_OK);
		EXPECT(!std::strcmp(leaf, "Hero.bes"));
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, user.c_str(), game.c_str(), leaf,
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "Hero.bes"));

		CeCareerPublishRequest req{};
		req.userDir = user.c_str();
		req.gameDir = game.c_str();
		req.packageDir = pkg.c_str();
		req.backupRoot = backup.c_str();
		req.sourceName = leaf;
		req.targetName = target;
		req.bytes = bytes;
		req.size = CE_CAREER_SIZE;
		req.permitted = permitCb;
		req.namePermitted = nameCb;
		const CeCareerPublishResult res = CeCareerPublishNew(&req);
		EXPECT(res.status == CE_PUBLISH_COMPLETE);
		EXPECT(res.possible == 1);
		EXPECT(!std::strcmp(res.newFile, "Hero.bes"));
		EXPECT(res.backupSet[0] != 0);
		EXPECT(gTakeFullSetCalls == 1);
		EXPECT(gPermitCalls >= 1 && gNameCalls >= 1);
		std::printf("target=%s\nbackup=%s\n", res.newFile, res.backupSet);
	}
	else if (mode == "edit_ok")
	{
		std::vector<unsigned char> edited;
		EXPECT(readFile(extFile, edited));
		char target[80]{};
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_EDIT, user.c_str(), game.c_str(), "Existing.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "Existing edited.bes"));

		CeCareerPublishRequest req{};
		req.userDir = user.c_str();
		req.gameDir = game.c_str();
		req.packageDir = pkg.c_str();
		req.backupRoot = backup.c_str();
		req.sourceName = "Existing.bes";
		req.targetName = target;
		req.bytes = edited.data();
		req.size = edited.size();
		req.permitted = permitCb;
		req.namePermitted = nameCb;
		const CeCareerPublishResult res = CeCareerPublishNew(&req);
		EXPECT(res.status == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(res.newFile, "Existing edited.bes"));
		EXPECT(gTakeFullSetCalls == 1);
	}
	else if (mode == "suggest_and_reserved")
	{
		char target[80]{};
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, user.c_str(), game.c_str(), "Existing.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "Existing imported.bes"));
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, user.c_str(), game.c_str(), "Again.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "Again imported 2.bes"));
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, user.c_str(), game.c_str(), "COM0.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "Imported.bes"));
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, user.c_str(), game.c_str(), "LPT0.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "Imported.bes"));

		std::vector<unsigned char> payload;
		EXPECT(readFile(extFile, payload));
		for (const char *badTarget : {"COM0.bes", "LPT0.bes"})
		{
			CeCareerPublishRequest req{};
			req.userDir = user.c_str();
			req.gameDir = game.c_str();
			req.packageDir = pkg.c_str();
			req.backupRoot = backup.c_str();
			req.sourceName = "Hero.bes";
			req.targetName = badTarget;
			req.bytes = payload.data();
			req.size = payload.size();
			req.permitted = permitCb;
			req.namePermitted = nameCb;
			const CeCareerPublishResult res = CeCareerPublishNew(&req);
			EXPECT(res.status == CE_PUBLISH_BAD_NAME);
			EXPECT(res.possible == 0 && res.newFile[0] == 0);
		}
		EXPECT(gTakeFullSetCalls == 0);
	}
	else if (mode == "occupied_user" || mode == "occupied_game")
	{
		std::vector<unsigned char> payload;
		EXPECT(readFile(extFile, payload));
		CeCareerPublishRequest req{};
		req.userDir = user.c_str();
		req.gameDir = game.c_str();
		req.packageDir = pkg.c_str();
		req.backupRoot = backup.c_str();
		req.sourceName = "Hero.bes";
		req.targetName = (mode == "occupied_user") ? "existing.bes" : "GameSlot.bes";
		req.bytes = payload.data();
		req.size = payload.size();
		req.permitted = permitCb;
		req.namePermitted = nameCb;
		const CeCareerPublishResult res = CeCareerPublishNew(&req);
		EXPECT(res.status == CE_PUBLISH_OCCUPIED);
		EXPECT(res.possible == 0 && res.newFile[0] == 0);
		EXPECT(gTakeFullSetCalls == 0);
	}
	else if (mode == "lock_busy")
	{
		std::vector<unsigned char> payload;
		EXPECT(readFile(extFile, payload));
		gHoldLockAfterBackup = true;
		CeCareerPublishRequest req{};
		req.userDir = user.c_str();
		req.gameDir = game.c_str();
		req.packageDir = pkg.c_str();
		req.backupRoot = backup.c_str();
		req.sourceName = "Hero.bes";
		req.targetName = "Fresh.bes";
		req.bytes = payload.data();
		req.size = payload.size();
		req.permitted = permitCb;
		req.namePermitted = nameCb;
		const CeCareerPublishResult res = CeCareerPublishNew(&req);
		EXPECT(res.status == CE_PUBLISH_BUSY);
		EXPECT(res.possible == 0 && res.newFile[0] == 0);
		if (gHeldLockHandle != INVALID_HANDLE_VALUE)
		{
			OVERLAPPED ov{};
			UnlockFileEx(gHeldLockHandle, 0, 1, 0, &ov);
			CloseHandle(gHeldLockHandle);
			gHeldLockHandle = INVALID_HANDLE_VALUE;
		}
	}
	else if (mode == "no_backup_folder" || mode == "backup_inside_user" ||
			 mode == "user_inside_backup" || mode == "backup_refused" ||
			 mode == "authority_refused" || mode == "name_refused")
	{
		std::vector<unsigned char> payload;
		EXPECT(readFile(extFile, payload));
		if (mode == "backup_refused")
			gFailBackup = true;
		if (mode == "authority_refused")
			gPermit = false;
		if (mode == "name_refused")
			gNamePermit = false;

		const std::string insideBackup = winJoin(user, "subbackup");
		CeCareerPublishRequest req{};
		req.userDir = user.c_str();
		req.gameDir = game.c_str();
		req.packageDir = pkg.c_str();
		req.backupRoot = (mode == "no_backup_folder") ? ""
					   : (mode == "backup_inside_user") ? insideBackup.c_str()
					   : (mode == "user_inside_backup") ? gBase.c_str()
														: backup.c_str();
		req.sourceName = "Hero.bes";
		req.targetName = "Fresh.bes";
		req.bytes = payload.data();
		req.size = payload.size();
		req.permitted = permitCb;
		req.namePermitted = nameCb;
		const CeCareerPublishResult res = CeCareerPublishNew(&req);
		if (mode == "no_backup_folder")
			EXPECT(res.status == CE_PUBLISH_NO_FOLDER);
		else if (mode == "authority_refused")
			EXPECT(res.status == CE_PUBLISH_AUTHORITY);
		else if (mode == "name_refused")
			EXPECT(res.status == CE_PUBLISH_BAD_NAME);
		else
			EXPECT(res.status == CE_PUBLISH_REFUSED);
		if (mode == "backup_inside_user" || mode == "user_inside_backup" ||
			mode == "no_backup_folder" || mode == "authority_refused" || mode == "name_refused")
			EXPECT(gTakeFullSetCalls == 0);
		EXPECT(res.possible == 0 && res.newFile[0] == 0);
	}
	else if (mode == "read_refusals")
	{
		unsigned char bytes[CE_CAREER_SIZE]{};
		char leaf[256]{}, err[192]{};
		auto checkRead = [&](const std::string &path)
		{
			return CeCareerImportRead(path.c_str(), user.c_str(), game.c_str(), pkg.c_str(),
									  bytes, sizeof(bytes), leaf, sizeof(leaf), err, sizeof(err));
		};
		const std::string extDir = winJoin(gBase, "external");
		std::wstring wSrc, wHard, wHidden;
		EXPECT(ce_backup_paths::wide(winJoin(extDir, "Hero.bes").c_str(), wSrc));
		EXPECT(ce_backup_paths::wide(winJoin(extDir, "hardlink.bes").c_str(), wHard));
		EXPECT(ce_backup_paths::wide(winJoin(extDir, "hidden.bes").c_str(), wHidden));
		EXPECT(CreateHardLinkW(wHard.c_str(), wSrc.c_str(), nullptr) != FALSE);
		EXPECT(SetFileAttributesW(wHidden.c_str(), FILE_ATTRIBUTE_HIDDEN) != FALSE);

		EXPECT(checkRead(winJoin(extDir, "hardlink.bes")) == CE_IMPORT_READ_UNSAFE_FILE);
		EXPECT(checkRead(winJoin(extDir, "hidden.bes")) == CE_IMPORT_READ_UNSAFE_FILE);
		EXPECT(checkRead(winJoin(winJoin(user, "savegames"), "Existing.bes")) == CE_IMPORT_READ_BAD_PATH);
		EXPECT(checkRead(winJoin(winJoin(game, "savegames"), "GameSlot.bes")) == CE_IMPORT_READ_BAD_PATH);
		EXPECT(checkRead(winJoin(pkg, "Packaged.bes")) == CE_IMPORT_READ_BAD_PATH);
		EXPECT(checkRead(winJoin(extDir, "short.bes")) == CE_IMPORT_READ_BAD_SIZE);
		EXPECT(checkRead(winJoin(extDir, "long.bes")) == CE_IMPORT_READ_BAD_SIZE);
		EXPECT(checkRead(winJoin(extDir, "empty.bes")) == CE_IMPORT_READ_BAD_SIZE);
		EXPECT(checkRead(winJoin(extDir, "badver.bes")) == CE_IMPORT_READ_BAD_CAREER);
		EXPECT(checkRead(winJoin(extDir, "implausible.bes")) == CE_IMPORT_READ_BAD_CAREER);
		EXPECT(checkRead(winJoin(extDir, "badgoodie.bes")) == CE_IMPORT_READ_BAD_CAREER);
		EXPECT(checkRead(winJoin(extDir, "bidi\xe2\x80\xae.bes")) == CE_IMPORT_READ_BAD_PATH);
	}
	else if (mode == "picker_lifecycle")
	{
		CeImportPickStatus st{};
		CeCareerImportPickEnter();
		CeCareerImportPickOpen();
		SDL_DialogFileCallback staleCb = gSavedCallback;
		void *staleData = gSavedUserdata;
		CeCareerImportPickLeave();
		const char *validList[] = {extFile.c_str(), nullptr};
		staleCb(staleData, validList, 0);
		CeCareerImportPickPoll(&st);
		EXPECT(st.state == CE_IMPORT_PICK_IDLE && st.path[0] == 0);

		CeCareerImportPickEnter();
		CeCareerImportPickOpen();
		const char *unixList[] = {"/tmp/Hero.bes", nullptr};
		gSavedCallback(gSavedUserdata, unixList, 0);
		CeCareerImportPickPoll(&st);
		EXPECT(st.state == CE_IMPORT_PICK_ERROR);

		CeCareerImportPickOpen();
		const char *multiList[] = {extFile.c_str(), extFile.c_str(), nullptr};
		gSavedCallback(gSavedUserdata, multiList, 0);
		CeCareerImportPickPoll(&st);
		EXPECT(st.state == CE_IMPORT_PICK_ERROR);

		CeCareerImportPickOpen();
		gSavedCallback(gSavedUserdata, validList, 0);
		CeCareerImportPickPoll(&st);
		EXPECT(st.state == CE_IMPORT_PICK_READY);
		EXPECT(!std::strcmp(st.leaf, "Hero.bes"));
		CeCareerImportPickLeave();
	}
	else if (mode == "rename_race_occupied")
	{
		std::vector<unsigned char> payload, racer;
		EXPECT(readFile(extFile, payload));
		EXPECT(readFile(winJoin(winJoin(user, "savegames"), "Existing.bes"), racer));
		gRaceBytes = racer;
		gRaceCreateTarget = true;

		CeCareerPublishRequest req{};
		req.userDir = user.c_str();
		req.gameDir = game.c_str();
		req.packageDir = pkg.c_str();
		req.backupRoot = backup.c_str();
		req.sourceName = "Hero.bes";
		req.targetName = "Hero.bes";
		req.bytes = payload.data();
		req.size = payload.size();
		req.permitted = permitCb;
		req.namePermitted = nameCb;
		const CeCareerPublishResult res = CeCareerPublishNew(&req);
		EXPECT(res.status == CE_PUBLISH_OCCUPIED);
		EXPECT(res.possible == 0 && res.newFile[0] == 0);
	}
	else if (mode == "absent_savegames_dirs")
	{
		std::vector<unsigned char> payload;
		EXPECT(readFile(extFile, payload));
		char target[80]{};

		auto winExists = [](const std::string &p) {
			std::wstring wp;
			return ce_backup_paths::wide(p.c_str(), wp) && GetFileAttributesW(wp.c_str()) != INVALID_FILE_ATTRIBUTES;
		};
		auto makeReq = [&](const std::string &u, const std::string &g, const std::string &p, const std::string &b) {
			CeCareerPublishRequest req{};
			req.userDir = u.c_str();
			req.gameDir = g.c_str();
			req.packageDir = p.c_str();
			req.backupRoot = b.c_str();
			req.sourceName = "Hero.bes";
			req.targetName = "Hero.bes";
			req.bytes = payload.data();
			req.size = payload.size();
			req.permitted = permitCb;
			req.namePermitted = nameCb;
			return req;
		};

		// 1) game\savegames absent, user\savegames populated.
		const std::string g1 = winJoin(winJoin(gBase, "no_game_saves"), "game");
		const std::string u1 = winJoin(winJoin(gBase, "no_game_saves"), "user");
		const std::string p1 = winJoin(winJoin(gBase, "no_game_saves"), "package");
		const std::string b1 = winJoin(winJoin(gBase, "no_game_saves"), "backup");
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, u1.c_str(), g1.c_str(), "Existing.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "Existing imported.bes"));
		CeCareerPublishRequest req1 = makeReq(u1, g1, p1, b1);
		EXPECT(CeCareerPublishNew(&req1).status == CE_PUBLISH_COMPLETE);
		EXPECT(!winExists(winJoin(g1, "savegames")));
		EXPECT(winExists(winJoin(winJoin(u1, "savegames"), "Hero.bes")));

		// 2) user\savegames absent, game\savegames populated: suggest + failed gates leave user\savegames absent.
		const std::string g2 = winJoin(winJoin(gBase, "no_user_saves"), "game");
		const std::string u2 = winJoin(winJoin(gBase, "no_user_saves"), "user");
		const std::string p2 = winJoin(winJoin(gBase, "no_user_saves"), "package");
		const std::string b2 = winJoin(winJoin(gBase, "no_user_saves"), "backup");
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, u2.c_str(), g2.c_str(), "GameSlot.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_COMPLETE);
		EXPECT(!std::strcmp(target, "GameSlot imported.bes"));
		EXPECT(!winExists(winJoin(u2, "savegames")));

		CeCareerPublishRequest req2 = makeReq(u2, g2, p2, b2);
		gPermit = false;
		EXPECT(CeCareerPublishNew(&req2).status == CE_PUBLISH_AUTHORITY);
		EXPECT(!winExists(winJoin(u2, "savegames")));
		gPermit = true;

		gFailBackup = true;
		EXPECT(CeCareerPublishNew(&req2).status == CE_PUBLISH_REFUSED);
		EXPECT(!winExists(winJoin(u2, "savegames")));
		gFailBackup = false;

		EXPECT(CeCareerPublishNew(&req2).status == CE_PUBLISH_COMPLETE);
		EXPECT(winExists(winJoin(winJoin(u2, "savegames"), "Hero.bes")));

		// 3) Regular file at savegames is refused.
		const std::string g3 = winJoin(winJoin(gBase, "file_at_saves"), "game");
		const std::string u3 = winJoin(winJoin(gBase, "file_at_saves"), "user");
		const std::string p3 = winJoin(winJoin(gBase, "file_at_saves"), "package");
		const std::string b3 = winJoin(winJoin(gBase, "file_at_saves"), "backup");
		EXPECT(CeCareerPublishSuggest(CE_PUBLISH_IMPORT, u3.c_str(), g3.c_str(), "Hero.bes",
									  nameCb, nullptr, target, sizeof(target)) == CE_PUBLISH_REFUSED);
		CeCareerPublishRequest req3 = makeReq(u3, g3, p3, b3);
		EXPECT(CeCareerPublishNew(&req3).status == CE_PUBLISH_REFUSED);
	}
	else
		return 2;

	std::printf("RESULT %s\n", gFails ? "fail" : "pass");
	return gFails ? 1 : 0;
}

#endif
