/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#pragma once

#include "Core/API/Texture.h"

namespace Falcor
{
    /** SDF grid base class, stored by distance values at grid cell/voxel corners.
        The local space of the SDF grid is [-0.5, 0.5]^3 meaning that initial distances used to create the SDF grid should be within the range of [-sqrt(3), sqrt(3)].

        SDF grid implementations create AABBs that should be used as procedural primitives to create acceleration structures.
        SDF grids can currently not be rendered using rasterization.
        Instead, SDF grids must be built into an acceleration structure and may then be ray traced using an intersection shader or inline intersection test.
    */
    class dlldecl SDFGrid
    {
    public:
        using SharedPtr = std::shared_ptr<SDFGrid>;

        virtual ~SDFGrid() = default;

        /** Create a new, empty SDF grid.
           \return dlldecl object, or nullptr if errors occurred.
       */
        static SharedPtr create();

        /** Set the signed distance values of the SDF grid, values are expected to be at the corners of voxels.
            \param[in] cornerValues The corner values for all voxels in the grid.
            \param[in] gridWidth The grid width, note that this represents the grid width in voxels, not in values, i.e., cornerValues should have a size of (gridWidth + 1)^3.
            \param[in] narrowBandThickness SDF grid implementations operate on normalized distances, the distances are normalized so that a normalized distance of +- 1 represents a distance of "narrowBandThickness" voxel diameters. Should not be less than 1.
            \return true if the values could be set, otherwise false.
        */
        bool setValues(const std::vector<float>& cornerValues, uint32_t gridWidth, float narrowBandThickness);

        /** Set the signed distance values of the SDF grid from a file.
            \param[in] filename The name of a .sdfg file.
            \param[in] narrowBandThickness SDF grid implementations operate on normalized distances, the distances are normalized so that a normalized distance of +- 1 represents a distance of "narrowBandThickness" voxel diameters. Should not be less than 1.
            \return true if the values could be set, otherwise false.
        */
        bool loadValuesFromFile(const std::string& filename, float narrowBandThickness);

        /** Calculates the appropriate normalization factor given a grid width (in voxels).
        */
        float calculateNormalizationFactor(uint32_t gridWidth);

        /** Returns the width of the SDF grid in voxels.
        */
        virtual uint32_t getGridWidth() const { return mGridWidth; }

        /** Returns the narrow band thickness of the SDF grid.
        */
        virtual float getNarrowBandThickness() const { return mNarrowBandThickness; }

        /** Get the name of the SDF grid.
            \return Returns the name.
        */
        const std::string& getName() const { return mName; }

        /** Set the name of the SDF grid.
            \param[in] name Name to set.
        */
        void setName(const std::string& name) { mName = name; }

        /** Returns the byte size of the SDF grid.
        */
        virtual size_t getSize() const = 0;

        /** Returns the maximum number of bits that could be stored in the primitive ID field of HitInfo.
        */
        virtual uint32_t getMaxPrimitiveIDBits() const = 0;

        /** Creates the GPU data structures required to render the SDF grid.
        */
        virtual bool createResources(RenderContext* pRenderContext = nullptr, bool deleteScratchData = true) = 0;

        /** Binds the SDF grid into a given shader var.
        */
        virtual void setShaderData(const ShaderVar& var) const = 0;

    protected:
        virtual bool setValuesInternal(const std::vector<float>& cornerValues) = 0;

        std::string mName;
        uint32_t mGridWidth = 0;
        float mNarrowBandThickness = 0.0f;
    };
}
