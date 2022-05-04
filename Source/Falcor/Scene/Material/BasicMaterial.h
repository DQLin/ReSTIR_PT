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
#include "Material.h"
#include "MaterialDefines.slangh"

namespace Falcor
{
    /** Base class for basic non-layered materials.

        Texture channel layout:

            Emissive
                - RGB - Emissive Color
                - A   - Unused
            Normal
                - 3-Channel standard normal map, or 2-Channel BC5 format
            Displacement
                - RGB - Displacement data
                - A   - Unused

        See additional texture channels defined in derived classes.
    */
    class dlldecl BasicMaterial : public Material
    {
    public:
        using SharedPtr = std::shared_ptr<BasicMaterial>;
        using SharedConstPtr = std::shared_ptr<const BasicMaterial>;

        // Implementation of Material interface

        /** Render the UI.
            \return True if the material was modified.
        */
        virtual bool renderUI(Gui::Widgets& widget) override;

        /** Returns true if the material is opaque.
        */
        bool isOpaque() const override { return getAlphaMode() == AlphaModeOpaque; }

        /** Returns true if material is emissive.
        */
        bool isEmissive() const override { return EXTRACT_EMISSIVE_TYPE(mData.flags) != ChannelTypeUnused; }

        /** Returns true if the material has a displacement map.
        */
        bool isDisplaced() const override;

        /** Compares material to another material.
            \param[in] pOther Other material.
            \return true if all materials properties *except* the name are identical.
        */
        bool isEqual(const Material::SharedPtr& pOther) const override;

        /** Get information about a texture slot.
            \param[in] slot The texture slot.
        */
        const TextureSlotInfo& getTextureSlotInfo(const TextureSlot slot) const override;

        /** Set one of the available texture slots.
            The call is ignored if the slot doesn't exist.
            \param[in] slot The texture slot.
            \param[in] pTexture The texture.
        */
        void setTexture(const TextureSlot slot, const Texture::SharedPtr& pTexture) override;

        /** Load one of the available texture slots.
            The call is ignored if the slot doesn't exist.
            \param[in] The texture slot.
            \param[in] filename Filename to load texture from.
            \param[in] useSrgb Load texture as sRGB format.
        */
        void loadTexture(const TextureSlot slot, const std::string& filename, bool useSrgb = true) override;

        /** Get one of the available texture slots.
            \param[in] The texture slot.
            \return Texture object if bound, or nullptr if unbound or slot doesn't exist.
        */
        Texture::SharedPtr getTexture(const TextureSlot slot) const override;

        /** Optimize texture usage for the given texture slot.
            This function may replace constant textures by uniform material parameters etc.
            \param[in] slot The texture slot.
            \param[in] texInfo Information about the texture bound to this slot.
            \param[out] stats Optimization stats passed back to the caller.
        */
        void optimizeTexture(const TextureSlot slot, const TextureAnalyzer::Result& texInfo, TextureOptimizationStats& stats) override;

        /** If material is displaced, prepares the displacement map in order to match the format required for rendering.
        */
        void prepareDisplacementMapForRendering() override;

        /** Bind a default texture sampler to the material.
        */
        void setDefaultTextureSampler(const Sampler::SharedPtr& pSampler) override;

        /** Get the default texture sampler attached to the material.
        */
        Sampler::SharedPtr getDefaultTextureSampler() const override { return mResources.samplerState; }


        // Additional member functions for BasicMaterial

        /** Set the base color texture
        */
        void setBaseColorTexture(Texture::SharedPtr pBaseColor) { setTexture(TextureSlot::BaseColor, pBaseColor); }

        /** Get the base color texture
        */
        Texture::SharedPtr getBaseColorTexture() const { return getTexture(TextureSlot::BaseColor); }

        /** Set the specular texture
        */
        void setSpecularTexture(Texture::SharedPtr pSpecular) { setTexture(TextureSlot::Specular, pSpecular); }

        /** Get the specular texture
        */
        Texture::SharedPtr getSpecularTexture() const { return getTexture(TextureSlot::Specular); }

        /** Set the emissive texture
        */
        void setEmissiveTexture(const Texture::SharedPtr& pEmissive) { setTexture(TextureSlot::Emissive, pEmissive); }

        /** Get the emissive texture
        */
        Texture::SharedPtr getEmissiveTexture() const { return getTexture(TextureSlot::Emissive); }

        /** Set the specular transmission texture
        */
        void setTransmissionTexture(const Texture::SharedPtr& pTransmission) { setTexture(TextureSlot::Transmission, pTransmission); }

        /** Get the specular transmission texture
        */
        Texture::SharedPtr getTransmissionTexture() const { return getTexture(TextureSlot::Transmission); }

        /** Set the normal map
        */
        void setNormalMap(Texture::SharedPtr pNormalMap) { setTexture(TextureSlot::Normal, pNormalMap); }

        /** Get the normal map
        */
        Texture::SharedPtr getNormalMap() const { return getTexture(TextureSlot::Normal); }

        /** Set the displacement map
        */
        void setDisplacementMap(Texture::SharedPtr pDisplacementMap) { setTexture(TextureSlot::Displacement, pDisplacementMap); }

        /** Get the displacement map
        */
        Texture::SharedPtr getDisplacementMap() const { return getTexture(TextureSlot::Displacement); }

        /** Set the displacement scale
        */
        void setDisplacementScale(float scale);

        /** Get the displacement scale
        */
        float getDisplacementScale() const { return mData.displacementScale; }

        /** Set the displacement offset
        */
        void setDisplacementOffset(float offset);

        /** Get the displacement offset
        */
        float getDisplacementOffset() const { return mData.displacementOffset; }

        /** Set the base color
        */
        void setBaseColor(const float4& color);

        /** Get the base color
        */
        const float4& getBaseColor() const { return mData.baseColor; }

        /** Set the specular parameters
        */
        void setSpecularParams(const float4& color);

        /** Get the specular parameters
        */
        const float4& getSpecularParams() const { return mData.specular; }

        /** Set the transmission color
        */
        void setTransmissionColor(const float3& transmissionColor);

        /** Get the transmission color
        */
        const float3& getTransmissionColor() const { return mData.transmission; }

        /** Set the diffuse transmission
        */
        void setDiffuseTransmission(float diffuseTransmission);

        /** Get the diffuse transmission
        */
        float getDiffuseTransmission() const { return mData.diffuseTransmission; }

        /** Set the specular transmission
        */
        void setSpecularTransmission(float specularTransmission);

        /** Get the specular transmission
        */
        float getSpecularTransmission() const { return mData.specularTransmission; }

        /** Set the volume absorption (absorption coefficient).
        */
        void setVolumeAbsorption(const float3& volumeAbsorption);

        /** Get the volume absorption (absorption coefficient).
        */
        const float3& getVolumeAbsorption() const { return mData.volumeAbsorption; }

        /** Set the volume scattering (scattering coefficient).
        */
        void setVolumeScattering(const float3& volumeScattering);

        /** Get the volume scattering (scattering coefficient).
        */
        const float3& getVolumeScattering() const { return mData.volumeScattering; }

        /** Set the volume phase function anisotropy (g).
        */
        void setVolumeAnisotropy(float volumeAnisotropy);

        /** Get the volume phase function anisotropy (g).
        */
        float getVolumeAnisotropy() const { return mData.volumeAnisotropy; }

        /** Set the emissive color
        */
        void setEmissiveColor(const float3& color);

        /** Set the emissive factor
        */
        void setEmissiveFactor(float factor);

        /** Get the emissive color
        */
        const float3& getEmissiveColor() const { return mData.emissive; }

        /** Get the emissive factor
        */
        float getEmissiveFactor() const { return mData.emissiveFactor; }

        /** Set the alpha mode
        */
        void setAlphaMode(uint32_t alphaMode);

        /** Get the alpha mode
        */
        uint32_t getAlphaMode() const { return EXTRACT_ALPHA_MODE(mData.flags); }

        /** Get the normal map type.
        */
        uint32_t getNormalMapType() const { return EXTRACT_NORMAL_MAP_TYPE(mData.flags); }

        /** Set the double-sided flag. This flag doesn't affect the rasterizer state, just the shading
        */
        void setDoubleSided(bool doubleSided);

        /** Returns true if the material is double-sided
        */
        bool isDoubleSided() const { return EXTRACT_DOUBLE_SIDED(mData.flags); }

        /** Set the alpha threshold. The threshold is only used if the alpha mode is `AlphaModeMask`
        */
        void setAlphaThreshold(float alpha);

        /** Get the alpha threshold
        */
        float getAlphaThreshold() const { return mData.alphaThreshold; }

        /** Get the flags
        */
        uint32_t getFlags() const { return mData.flags; }

        /** Set the index of refraction
        */
        void setIndexOfRefraction(float IoR);

        /** Get the index of refraction
        */
        float getIndexOfRefraction() const { return mData.IoR; }

        /** Set the nested priority used for nested dielectrics
        */
        void setNestedPriority(uint32_t priority);

        /** Get the nested priority used for nested dielectrics.
            \return Nested priority, with 0 reserved for the highest possible priority.
        */
        uint32_t getNestedPriority() const { return EXTRACT_NESTED_PRIORITY(mData.flags); }

        /** Set the thin surface flag
        */
        void setThinSurface(bool thinSurface);

        /** Returns true if the material is a thin surface
        */
        bool isThinSurface() const { return EXTRACT_THIN_SURFACE(mData.flags); }

        /** Comparison operator.
            \return True if all materials properties *except* the name are identical.
        */
        bool operator==(const BasicMaterial& other) const;

        /** Returns the material data struct.
        */
        const MaterialData& getData() const { return mData; }

        /** Returns the material resources struct.
        */
        const MaterialResources& getResources() const { return mResources; }

    protected:
        BasicMaterial(const std::string& name);

        void setFlags(uint32_t flags);
        void updateBaseColorType();
        void updateSpecularType();
        void updateEmissiveType();
        void updateTransmissionType();
        void updateAlphaMode();
        void updateNormalMapMode();
        void updateDoubleSidedFlag();
        void updateDisplacementFlag();

        virtual void renderSpecularUI(Gui::Widgets& widget) {}

        MaterialData mData;                         ///< Material parameters.
        MaterialResources mResources;               ///< Material textures and samplers.
        bool mDoubleSided = false;

        std::array<TextureSlotInfo, (size_t)TextureSlot::Count> mTextureSlotInfo;

        // Additional data to optimize texture access.
        float2 mAlphaRange = float2(0.f, 1.f);      ///< Conservative range of opacity (alpha) values for the material.
        bool mIsTexturedBaseColorConstant = false;  ///< Flag indicating if the color channels of the base color texture are constant.
        bool mIsTexturedAlphaConstant = false;      ///< Flag indicating if the alpha channel of the base color texture is constant.

        friend class SceneCache;
    };
}
