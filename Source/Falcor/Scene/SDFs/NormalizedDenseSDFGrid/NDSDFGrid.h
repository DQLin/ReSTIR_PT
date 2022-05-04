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

#include "Scene/SDFs/SDFGrid.h"
#include "Core/API/Texture.h"

namespace Falcor
{
    /** A normalized dense SDF grid, represented as a set of textures. Can only be accessed on the GPU.
    */
    class dlldecl NDSDFGrid : public SDFGrid
    {
    public:
        using SharedPtr = std::shared_ptr<NDSDFGrid>;

        /** Create a new, empty normalized dense SDF grid.
            \return NDSDFGrid object, or nullptr if errors occurred.
        */
        static SharedPtr create();

        virtual size_t getSize() const override;

        virtual uint32_t getMaxPrimitiveIDBits() const override { return bitScanReverse(uint32_t(mValues.size() - 1)) + 1; }

        virtual bool createResources(RenderContext* pRenderContext = nullptr, bool deleteScratchData = true) override;

        virtual void setShaderData(const ShaderVar& var) const override;

    protected:
        virtual bool setValuesInternal(const std::vector<float>& cornerValues) override;

    private:
        NDSDFGrid() = default;

        // CPU data.
        std::vector<std::vector<uint8_t>> mValues;

        // Specs.
        uint32_t mCoarsestLODGridWidth = 0;
        float mCoarsestLODNormalizationFactor = 0.0f;

        // Grid Sampler, shared among all NDSDFGrids.
        static Sampler::SharedPtr sNDSDFGridSampler;

        // GPU data.
        std::vector<Texture::SharedPtr> mNDSDFTextures;
    };
}
