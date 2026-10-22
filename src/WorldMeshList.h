// WorldMeshList.h: the names of the things a level's world uses, collected so their meshes can be loaded.
// Reconstructed from WorldMeshList.obj (0050d870..0050de8f): the item constructor copies the name with an
// allocation tagged with this header and line 17 (0050db43).

#ifndef WORLD_MESH_LIST_H
#define WORLD_MESH_LIST_H

#include "SPtrSet.h"

// One name (8 bytes, MEMTYPE_WORLDMESHLIST).
class CWorldMeshListItem
{
public:
	CWorldMeshListItem(char *name)
	{
		mUsed = FALSE;
		mName = new(MEMTYPE_WORLDMESHLIST) char[strlen(name) + 1]; strcpy(mName, name);
	}

	char		*mName;						// +0x00
	BOOL		mUsed;						// +0x04: set by MarkUsed
};

class CWorldMeshList
{
public:
	static void	Clear();					// 0050d9a0
	static void	Add(char *name);			// 0050d9e0
	static void	MarkUsed(char *name);		// 0050dc20
	static void	SpawnUnused();				// 0050dcb0

	static SPtrSet<CWorldMeshListItem>	mList;	// 00855358
};

#endif
