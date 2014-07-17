//
//   Copyright 2013 Pixar
//
//   Licensed under the Apache License, Version 2.0 (the "Apache License")
//   with the following modification; you may not use this file except in
//   compliance with the Apache License and the following modification to it:
//   Section 6. Trademarks. is deleted and replaced with:
//
//   6. Trademarks. This License does not grant permission to use the trade
//      names, trademarks, service marks, or product names of the Licensor
//      and its affiliates, except as required to comply with Section 4(c) of
//      the License and to reproduce the content of the NOTICE file.
//
//   You may obtain a copy of the Apache License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the Apache License with the above modification is
//   distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//   KIND, either express or implied. See the Apache License for the specific
//   language governing permissions and limitations under the Apache License.
//

#ifndef OSD_MESH_H
#define OSD_MESH_H

#include "../version.h"

#include "../far/kernelBatch.h"
#include "../far/refineTables.h"
#include "../far/patchTablesFactory.h"
#include "../far/stencilTablesFactory.h"

#include "../osd/vertex.h"
#include "../osd/vertexDescriptor.h"

#include <bitset>
#include <cassert>

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

enum OsdMeshBits {
    MeshAdaptive    = 0,

    MeshPtexData    = 1,
    MeshFVarData    = 2,

    NUM_MESH_BITS   = 3,
};
typedef std::bitset<NUM_MESH_BITS> OsdMeshBitset;

template <class DRAW_CONTEXT>
class OsdMeshInterface {
public:
    typedef DRAW_CONTEXT DrawContext;
    typedef typename DrawContext::VertexBufferBinding VertexBufferBinding;

public:
    OsdMeshInterface() { }

    virtual ~OsdMeshInterface() { }

    virtual int GetNumVertices() const = 0;

    virtual void UpdateVertexBuffer(float const *vertexData, int startVertex, int numVerts) = 0;

    virtual void UpdateVaryingBuffer(float const *varyingData, int startVertex, int numVerts) = 0;

    virtual void Refine() = 0;

    virtual void Refine(OsdVertexBufferDescriptor const *vertexDesc,
                        OsdVertexBufferDescriptor const *varyingDesc,
                        bool interleaved) = 0;

    virtual void Synchronize() = 0;

    virtual DrawContext * GetDrawContext() = 0;

    virtual VertexBufferBinding BindVertexBuffer() = 0;

    virtual VertexBufferBinding BindVaryingBuffer() = 0;

protected:

    static inline int getNumVertices(FarRefineTables const & refTables) {
        return refTables.IsUniform() ?
            refTables.GetNumVertices(0) + refTables.GetNumVertices(refTables.GetMaxLevel()) :
                refTables.GetNumVerticesTotal();
    }

    static inline void refineMesh(FarRefineTables & refTables, int level, bool adaptive) {
        if (adaptive) {
            refTables.RefineAdaptive(level);
        } else {
            refTables.RefineUniform(level);
        }
    }
};



template <class VERTEX_BUFFER, class COMPUTE_CONTROLLER, class DRAW_CONTEXT>
class OsdMesh : public OsdMeshInterface<DRAW_CONTEXT> {
public:
    typedef VERTEX_BUFFER VertexBuffer;
    typedef COMPUTE_CONTROLLER ComputeController;
    typedef typename ComputeController::ComputeContext ComputeContext;
    typedef DRAW_CONTEXT DrawContext;
    typedef typename DrawContext::VertexBufferBinding VertexBufferBinding;

    OsdMesh(ComputeController * computeController,
            FarRefineTables * refTables,
            int numVertexElements,
            int numVaryingElements,
            int level,
            OsdMeshBitset bits = OsdMeshBitset()) :

            _refTables(refTables),
            _vertexBuffer(0),
            _varyingBuffer(0),
            _computeContext(0),
            _computeController(computeController),
            _drawContext(0) {

        assert(_refTables);

        OsdMeshInterface<DRAW_CONTEXT>::refineMesh(*_refTables, level, bits.test(MeshAdaptive));

        initializeVertexBuffers(numVertexElements, numVaryingElements, bits);
    }

    OsdMesh(ComputeController * computeController,
            FarRefineTables * refTables,
            VertexBuffer * vertexBuffer,
            VertexBuffer * varyingBuffer,
            ComputeContext * computeContext,
            DrawContext * drawContext) :

            _refTables(refTables),
            _vertexBuffer(vertexBuffer),
            _varyingBuffer(varyingBuffer),
            _computeContext(computeContext),
            _computeController(computeController),
            _drawContext(drawContext) { }

    virtual ~OsdMesh() {
        delete _refTables;
        delete _vertexBuffer;
        delete _varyingBuffer;
        delete _computeContext;
        delete _drawContext;
    }

    virtual int GetNumVertices() const {
        assert(_refTables);
        return OsdMeshInterface<DRAW_CONTEXT>::getNumVertices(*_refTables);
    }

    virtual void UpdateVertexBuffer(float const *vertexData, int startVertex, int numVerts) {
        _vertexBuffer->UpdateData(vertexData, startVertex, numVerts);
    }

    virtual void UpdateVaryingBuffer(float const *varyingData, int startVertex, int numVerts) {
        _varyingBuffer->UpdateData(varyingData, startVertex, numVerts);
    }

    virtual void Refine() {
        _computeController->Compute(_computeContext, _kernelBatches, _vertexBuffer, _varyingBuffer);
    }

    virtual void Refine(OsdVertexBufferDescriptor const *vertexDesc, OsdVertexBufferDescriptor const *varyingDesc) {
        assert(0); //_computeController->Refine(_computeContext, _kernelBatches, _vertexBuffer, _varyingBuffer, vertexDesc, varyingDesc);
    }

    virtual void Synchronize() {
        _computeController->Synchronize();
    }

    virtual VertexBufferBinding BindVertexBuffer() {
        return VertexBufferBinding(0);
    }

    virtual VertexBufferBinding BindVaryingBuffer() {
        return VertexBufferBinding(0);
    }

    virtual DrawContext * GetDrawContext() {
        return _drawContext;
    }

private:

    void initializeVertexBuffers(int numVertexElements,
        int numVaryingElements, OsdMeshBitset bits) {

        int numVertices = OsdMeshInterface<DRAW_CONTEXT>::getNumVertices(*_refTables);

        if (numVertexElements)
            _vertexBuffer = VertexBuffer::Create(numVertexElements, numVertices);

        if (numVaryingElements)
            _varyingBuffer = VertexBuffer::Create(numVaryingElements, numVertices);

        {
            FarStencilTablesFactory::Options options;
            options.generateOffsets=true;
            options.generateAllLevels = _refTables->IsUniform() ? false : true;

            FarStencilTables const * stencilTables =
                FarStencilTablesFactory::Create(*_refTables, options);

            _kernelBatches.push_back(FarStencilTablesFactory::Create(*stencilTables));

            _computeContext = ComputeContext::Create(stencilTables);

            delete stencilTables;
        }

        {
            FarPatchTables const * patchTables =
                FarPatchTablesFactory::Create(*_refTables);

            _drawContext = DrawContext::Create(
                *patchTables, numVertexElements, bits.test(MeshFVarData));

            delete patchTables;
        }
    }

    FarRefineTables * _refTables;
    FarKernelBatchVector _kernelBatches;

    VertexBuffer * _vertexBuffer;
    VertexBuffer * _varyingBuffer;

    ComputeContext    * _computeContext;
    ComputeController * _computeController;

    DrawContext *_drawContext;
};

}  // end namespace OPENSUBDIV_VERSION
using namespace OPENSUBDIV_VERSION;

}  // end namespace OpenSubdiv

#endif  // OSD_MESH_H
