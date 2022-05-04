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
#include "stdafx.h"
#include "NDSDFGrid.h"

namespace Falcor
{
    Sampler::SharedPtr NDSDFGrid::sNDSDFGridSampler;

    NDSDFGrid::SharedPtr NDSDFGrid::create()
    {
        if (!sNDSDFGridSampler)
        {
            Sampler::Desc sdfGridSamplerDesc;
            sdfGridSamplerDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
            sdfGridSamplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp);
            sNDSDFGridSampler = Sampler::create(sdfGridSamplerDesc);
        }

        return SharedPtr(new NDSDFGrid());
    }

    size_t NDSDFGrid::getSize() const
    {
        size_t totalSize = 0;

        for (const Texture::SharedPtr& pNormalizedVolumeTexture : mNDSDFTextures)
        {
            totalSize += pNormalizedVolumeTexture->getTextureSizeInBytes();
        }

        return totalSize;
    }

    bool NDSDFGrid::createResources(RenderContext* pRenderContext, bool deleteScratchData)
    {
        uint32_t lodCount = (uint32_t)mValues.size();

        if (mNDSDFTextures.empty() || mNDSDFTextures.size() != lodCount)
        {
            mNDSDFTextures.clear();
            mNDSDFTextures.resize(lodCount);
        }

        for (uint32_t lod = 0; lod < lodCount; lod++)
        {
            uint32_t lodWidth = 1 + (mCoarsestLODGridWidth << lod);

            Texture::SharedPtr& pNDSDFTexture = mNDSDFTextures[lod];
            if (pNDSDFTexture && pNDSDFTexture->getWidth() == lodWidth)
            {
                pRenderContext->updateTextureData(pNDSDFTexture.get(), mValues[lod].data());
            }
            else
            {
                pNDSDFTexture = Texture::create3D(lodWidth, lodWidth, lodWidth, ResourceFormat::R8Snorm, 1, mValues[lod].data());
            }
        }

        return true;
    }

    bool NDSDFGrid::setValuesInternal(const std::vector<float>& cornerValues)
    {
        const uint32_t kCoarsestAllowedGridWidth = 8;

        if (kCoarsestAllowedGridWidth > mGridWidth)
        {
            logError("NDSDFGrid::setValues() grid width must be larger than " + std::to_string(kCoarsestAllowedGridWidth));
            return false;
        }

        uint32_t lodCount = bitScanReverse(mGridWidth / kCoarsestAllowedGridWidth) + 1;
        mCoarsestLODGridWidth = mGridWidth >> (lodCount - 1);
        mCoarsestLODNormalizationFactor = calculateNormalizationFactor(mCoarsestLODGridWidth);

        mValues.resize(lodCount);
        uint32_t gridWidthInValues = mGridWidth + 1;

        // Format all corner values to a normalized snorm8 format, where a distance of 1 represents "0.5 * narrowBandThickness" voxels of the current LOD.
        for (uint32_t lod = 0; lod < lodCount; lod++)
        {
            uint32_t lodWidthInVoxels = mCoarsestLODGridWidth << lod;
            uint32_t lodWidthInValues = 1 + lodWidthInVoxels;
            float normalizationFactor = mCoarsestLODNormalizationFactor / float(1 << lod);

            std::vector<uint8_t>& lodFormattedValues = mValues[lod];
            lodFormattedValues.resize(lodWidthInValues * lodWidthInValues * lodWidthInValues);

            uint32_t lodReadStride = 1 << (lodCount - lod - 1);
            for (uint32_t z = 0; z < lodWidthInValues; z++)
            {
                for (uint32_t y = 0; y < lodWidthInValues; y++)
                {
                    for (uint32_t x = 0; x < lodWidthInValues; x++)
                    {
                        uint32_t writeLocation = x + lodWidthInValues * (y + lodWidthInValues * z);
                        uint32_t readLocation = lodReadStride * (x + gridWidthInValues * (y + gridWidthInValues * z));

                        float normalizedValue = glm::clamp(cornerValues[readLocation] / normalizationFactor, -1.0f, 1.0f);

                        float integerScale = normalizedValue * float(INT8_MAX);
                        int8_t v = integerScale >= 0.0f ? int8_t(integerScale + 0.5f) : int8_t(integerScale - 0.5f);
                        std::memcpy(&lodFormattedValues[writeLocation], &v, sizeof(int8_t));
                    }
                }
            }
        }

        return true;
    }

    void NDSDFGrid::setShaderData(const ShaderVar& var) const
    {
        if (mNDSDFTextures.empty()) logError("NDSDFGrid::setShaderData() can't be called before calling NDSDFGrid::createResources()");

        auto ndGridVar = var["ndSDFGrid"];

        ndGridVar["sampler"] = sNDSDFGridSampler;
        ndGridVar["lodCount"] = uint32_t(mNDSDFTextures.size());
        ndGridVar["coarsestLODAsLevel"] = bitScanReverse(mCoarsestLODGridWidth);
        ndGridVar["coarsestLODGridWidth"] = mCoarsestLODGridWidth;
        ndGridVar["coarsestLODNormalizationFactor"] = mCoarsestLODNormalizationFactor;
        ndGridVar["narrowBandThickness"] = mNarrowBandThickness;

        auto texturesVar = ndGridVar["textures"];
        for (uint32_t lod = 0; lod < mNDSDFTextures.size(); lod++)
        {
            texturesVar[lod] = mNDSDFTextures[lod];
        }
    }
}
