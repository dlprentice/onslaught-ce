// DXMeshPart.h: CDXMeshPart, the mesh part the PC build allocates. Stuart's source (DXMeshPart.h) declares it as an
// empty class over CMeshPart, and MeshPart.h defines CMESHPART as it; its implicit inline constructor over the
// out-of-line CMeshPart constructor gives retail's new-expressions in CMesh::Load and CMesh::Deserialize (the raw
// pointer kept in a callee-saved register across the call).

#ifndef DXMESHPART_H
#define DXMESHPART_H

#include "MeshPart.h"

class	CDXMeshPart : public CMeshPart
{
public:
};

#endif // DXMESHPART_H
