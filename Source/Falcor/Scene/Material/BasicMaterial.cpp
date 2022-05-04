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
#include "BasicMaterial.h"
#include "Core/Program/GraphicsProgram.h"
#include "Core/Program/ProgramVars.h"
#include "Utils/Color/ColorHelpers.slang"

namespace Falcor
{
    namespace
    {
        static_assert(sizeof(MaterialData) % 16 == 0, "MaterialData size should be a multiple of 16");
        static_assert((MATERIAL_FLAGS_BITS) <= 32, "MaterialData flags should be maximum 32 bits");

        // Constants.
        const float kMaxVolumeAnisotropy = 0.99f;
    }

    BasicMaterial::BasicMaterial(const std::string& name)
        : Material(name)
    {
        // Setup common texture slots.
        mTextureSlotInfo[(uint32_t)TextureSlot::Emissive] = { "emissive", TextureChannelFlags::RGB, true };
        mTextureSlotInfo[(uint32_t)TextureSlot::Normal] = { "normal", TextureChannelFlags::RGB, false };
        mTextureSlotInfo[(uint32_t)TextureSlot::Displacement] = { "displacement", TextureChannelFlags::RGB, false };

        // Call update functions to ensure a valid initial state based on default material parameters.
        updateBaseColorType();
        updateSpecularType();
        updateEmissiveType();
        updateTransmissionType();
        updateAlphaMode();
        updateNormalMapMode();
        updateDisplacementFlag();
    }

    bool BasicMaterial::renderUI(Gui::Widgets& widget)
    {
        // Render the base class UI first.
        bool changed = Material::renderUI(widget);

        // We're re-using the material's update flags here to track changes.
        // Cache the previous flag so we can restore it before returning.
        UpdateFlags prevUpdates = mUpdates;
        mUpdates = UpdateFlags::None;

        if (const auto& tex = getBaseColorTexture(); tex != nullptr)
        {
            bool hasAlpha = doesFormatHasAlpha(tex->getFormat());
            bool alphaConst  = mIsTexturedAlphaConstant && hasAlpha;
            bool colorConst = mIsTexturedBaseColorConstant;

            std::ostringstream oss;
            oss << "Texture info: " << tex->getWidth() << "x" << tex->getHeight()
                << " (" << to_string(tex->getFormat()) << ")";
            if (colorConst && !alphaConst) oss << " (color constant)";
            else if (!colorConst && alphaConst) oss << " (alpha constant)";
            else if (colorConst && alphaConst) oss << " (color and alpha constant)"; // Shouldn't happen

            widget.text("Base color: " + tex->getSourceFilename());
            widget.text(oss.str());

            if (colorConst || alphaConst)
            {
                float4 baseColor = getBaseColor();
                if (widget.var("Base color", baseColor, 0.f, 1.f, 0.01f)) setBaseColor(baseColor);
            }

            widget.image("Base color", tex, float2(100.f));
            if (widget.button("Remove texture##BaseColor")) setBaseColorTexture(nullptr);
        }
        else
        {
            float4 baseColor = getBaseColor();
            if (widget.var("Base color", baseColor, 0.f, 1.f, 0.01f)) setBaseColor(baseColor);
        }

        if (const auto& tex = getSpecularTexture(); tex != nullptr)
        {
            widget.text("Specular params: " + tex->getSourceFilename());
            widget.text("Texture info: " + std::to_string(tex->getWidth()) + "x" + std::to_string(tex->getHeight()) + " (" + to_string(tex->getFormat()) + ")");
            widget.image("Specular params", tex, float2(100.f));
            if (widget.button("Remove texture##Specular")) setSpecularTexture(nullptr);
        }
        else
        {
            float4 specularParams = getSpecularParams();
            if (widget.var("Specular params", specularParams, 0.f, 1.f, 0.01f)) setSpecularParams(specularParams);
            widget.tooltip("The encoding depends on the material type");

            renderSpecularUI(widget); // Let derived classes draw additional UI elements.
        }

        if (const auto& tex = getNormalMap(); tex != nullptr)
        {
            widget.text("Normal map: " + tex->getSourceFilename());
            widget.text("Texture info: " + std::to_string(tex->getWidth()) + "x" + std::to_string(tex->getHeight()) + " (" + to_string(tex->getFormat()) + ")");
            widget.image("Normal map", tex, float2(100.f));
            if (widget.button("Remove texture##NormalMap")) setNormalMap(nullptr);
        }

        if (const auto& tex = getDisplacementMap(); tex != nullptr)
        {
            widget.text("Displacement map: " + tex->getSourceFilename());
            widget.text("Texture info: " + std::to_string(tex->getWidth()) + "x" + std::to_string(tex->getHeight()) + " (" + to_string(tex->getFormat()) + ")");
            widget.image("Displacement map", tex, float2(100.f));
            if (widget.button("Remove texture##DisplacementMap")) setDisplacementMap(nullptr);

            float scale = getDisplacementScale();
            if (widget.var("Displacement scale", scale)) setDisplacementScale(scale);

            float offset = getDisplacementOffset();
            if (widget.var("Displacement offset", offset)) setDisplacementOffset(offset);
        }

        if (const auto& tex = getEmissiveTexture(); tex != nullptr)
        {
            widget.text("Emissive color: " + tex->getSourceFilename());
            widget.text("Texture info: " + std::to_string(tex->getWidth()) + "x" + std::to_string(tex->getHeight()) + " (" + to_string(tex->getFormat()) + ")");
            widget.image("Emissive color", tex, float2(100.f));
            if (widget.button("Remove texture##Emissive")) setEmissiveTexture(nullptr);
        }
        else
        {
            float3 emissiveColor = getEmissiveColor();
            if (widget.var("Emissive color", emissiveColor, 0.f, 1.f, 0.01f)) setEmissiveColor(emissiveColor);
        }

        float emissiveFactor = getEmissiveFactor();
        if (widget.var("Emissive factor", emissiveFactor, 0.f, std::numeric_limits<float>::max(), 0.01f)) setEmissiveFactor(emissiveFactor);

        if (const auto& tex = getTransmissionTexture(); tex != nullptr)
        {
            widget.text("Transmission color: " + tex->getSourceFilename());
            widget.text("Texture info: " + std::to_string(tex->getWidth()) + "x" + std::to_string(tex->getHeight()) + " (" + to_string(tex->getFormat()) + ")");
            widget.image("Transmission color", tex, float2(100.f));
            if (widget.button("Remove texture##Transmission")) setTransmissionTexture(nullptr);
        }
        else
        {
            float3 transmissionColor = getTransmissionColor();
            if (widget.var("Transmission", transmissionColor, 0.f, 1.f, 0.01f)) setTransmissionColor(transmissionColor);
        }

        float diffuseTransmission = getDiffuseTransmission();
        if (widget.var("Diffuse transmission", diffuseTransmission, 0.f, 1.f, 0.01f)) setDiffuseTransmission(diffuseTransmission);

        float specularTransmission = getSpecularTransmission();
        if (widget.var("Specular transmission", specularTransmission, 0.f, 1.f, 0.01f)) setSpecularTransmission(specularTransmission);

        float IoR = getIndexOfRefraction();
        if (widget.var("Index of refraction", IoR, 1.f, std::numeric_limits<float>::max(), 0.01f)) setIndexOfRefraction(IoR);

        float3 volumeAbsorption = getVolumeAbsorption();
        if (widget.var("Absorption coefficient", volumeAbsorption, 0.f, std::numeric_limits<float>::max(), 0.01f)) setVolumeAbsorption(volumeAbsorption);

        float3 volumeScattering = getVolumeScattering();
        if (widget.var("Scattering coefficient", volumeScattering, 0.f, std::numeric_limits<float>::max(), 0.01f)) setVolumeScattering(volumeScattering);

        float volumeAnisotropy = getVolumeAnisotropy();
        if (widget.var("Anisotropy (g)", volumeAnisotropy, -1.f, 1.f, 0.01f)) setVolumeAnisotropy(volumeAnisotropy);

        uint32_t nestedPriority = getNestedPriority();
        if (widget.var("Nested priority", nestedPriority, 0u, (1u << NESTED_PRIORITY_BITS) - 1)) setNestedPriority(nestedPriority);

        bool thinSurface = isThinSurface();
        if (widget.checkbox("Thin surface", thinSurface)) setThinSurface(thinSurface);

        bool doubleSided = isDoubleSided();
        if (widget.checkbox("Double-sided", doubleSided)) setDoubleSided(doubleSided);

        // Show alpha parameters.
        // These are derived from other parameters and not directly editable.
        bool alphaTest = !isOpaque();
        widget.checkbox("Alpha test", alphaTest);

        float alphaThreshold = mData.alphaThreshold;
        widget.var("Alpha threshold", alphaThreshold);

        float2 alphaRange = mAlphaRange;
        widget.var("Alpha range", alphaRange);

        // Restore update flags.
        changed |= mUpdates != UpdateFlags::None;
        markUpdates(prevUpdates | mUpdates);

        return changed;
    }

    bool BasicMaterial::isDisplaced() const
    {
        return hasTextureSlot(Material::TextureSlot::Displacement) && getDisplacementMap() != nullptr;
    }

    void BasicMaterial::setAlphaMode(uint32_t alphaMode)
    {
        setFlags(PACK_ALPHA_MODE(mData.flags, alphaMode));
    }

    void BasicMaterial::setDoubleSided(bool doubleSided)
    {
        mDoubleSided = doubleSided;
        updateDoubleSidedFlag();
    }

    void BasicMaterial::setAlphaThreshold(float alpha)
    {
        if (mData.alphaThreshold != alpha)
        {
            mData.alphaThreshold = alpha;
            markUpdates(UpdateFlags::DataChanged);
            updateAlphaMode();
        }
    }

    void BasicMaterial::setIndexOfRefraction(float IoR)
    {
        if (mData.IoR != IoR)
        {
            mData.IoR = IoR;
            markUpdates(UpdateFlags::DataChanged);
        }
    }

    void BasicMaterial::setNestedPriority(uint32_t priority)
    {
        const uint32_t maxPriority = (1U << NESTED_PRIORITY_BITS) - 1;
        if (priority > maxPriority)
        {
            logWarning("Requested nested priority " + std::to_string(priority) + " for material '" + mName + "' is out of range. Clamping to " + std::to_string(maxPriority) + ".");
            priority = maxPriority;
        }
        setFlags(PACK_NESTED_PRIORITY(mData.flags, priority));
    }

    void BasicMaterial::setThinSurface(bool thinSurface)
    {
        setFlags(PACK_THIN_SURFACE(mData.flags, thinSurface ? 1 : 0));
    }

    void BasicMaterial::setDefaultTextureSampler(const Sampler::SharedPtr& pSampler)
    {
        if (pSampler != mResources.samplerState)
        {
            mResources.samplerState = pSampler;

            // Create derived samplers for displacement Min/Max filtering.
            Sampler::Desc desc = pSampler->getDesc();
            desc.setMaxAnisotropy(16);      // Set 16x anisotropic filtering for improved min/max precision per triangle.
            desc.setReductionMode(Sampler::ReductionMode::Min);
            mResources.displacementSamplerStateMin = Sampler::create(desc);
            desc.setReductionMode(Sampler::ReductionMode::Max);
            mResources.displacementSamplerStateMax = Sampler::create(desc);

            markUpdates(UpdateFlags::ResourcesChanged);
        }
    }

    void BasicMaterial::setTexture(const TextureSlot slot, const Texture::SharedPtr& pTexture)
    {
        if (!hasTextureSlot(slot))
        {
            logWarning("BasicMaterial::setTexture() - Material '" + getName() + "' does not have texture slot '" + to_string(slot) + "'. Ignoring call.");
            return;
        }

        if (pTexture == getTexture(slot)) return;

        switch (slot)
        {
        case TextureSlot::BaseColor:
            // Assume the texture is non-constant and has full alpha range.
            // This may be changed later by optimizeTexture().
            if (pTexture)
            {
                mAlphaRange = float2(0.f, 1.f);
                mIsTexturedBaseColorConstant = mIsTexturedAlphaConstant = false;
            }
            mResources.baseColor = pTexture;
            updateBaseColorType();
            updateAlphaMode();
            break;
        case TextureSlot::Specular:
            mResources.specular = pTexture;
            updateSpecularType();
            break;
        case TextureSlot::Emissive:
            mResources.emissive = pTexture;
            updateEmissiveType();
            break;
        case TextureSlot::Normal:
            mResources.normalMap = pTexture;
            updateNormalMapMode();
            break;
        case TextureSlot::Displacement:
            mResources.displacementMap = pTexture;
            updateDisplacementFlag();
            updateDoubleSidedFlag();
            break;
        case TextureSlot::Transmission:
            mResources.transmission = pTexture;
            updateTransmissionType();
            updateDoubleSidedFlag();
            break;
        default:
            should_not_get_here();
        }

        markUpdates(UpdateFlags::ResourcesChanged);
    }

    Texture::SharedPtr BasicMaterial::getTexture(const TextureSlot slot) const
    {
        if (!hasTextureSlot(slot)) return nullptr;

        switch (slot)
        {
        case TextureSlot::BaseColor:
            return mResources.baseColor;
        case TextureSlot::Specular:
            return mResources.specular;
        case TextureSlot::Emissive:
            return mResources.emissive;
        case TextureSlot::Normal:
            return mResources.normalMap;
        case TextureSlot::Displacement:
            return mResources.displacementMap;
        case TextureSlot::Transmission:
            return mResources.transmission;
        default:
            should_not_get_here();
        }
        return nullptr;
    }

    const Material::TextureSlotInfo& BasicMaterial::getTextureSlotInfo(const TextureSlot slot) const
    {
        assert((size_t)slot < mTextureSlotInfo.size());
        return mTextureSlotInfo[(size_t)slot];
    }

    void BasicMaterial::optimizeTexture(const TextureSlot slot, const TextureAnalyzer::Result& texInfo, TextureOptimizationStats& stats)
    {
        assert(getTexture(slot) != nullptr);
        TextureChannelFlags channelMask = getTextureSlotInfo(slot).mask;

        switch (slot)
        {
        case TextureSlot::BaseColor:
        {
            bool previouslyOpaque = isOpaque();

            bool hasAlpha = mResources.baseColor && doesFormatHasAlpha(mResources.baseColor->getFormat());
            bool isColorConstant = texInfo.isConstant(TextureChannelFlags::RGB);
            bool isAlphaConstant = texInfo.isConstant(TextureChannelFlags::Alpha);

            // Update the alpha range.
            if (hasAlpha) mAlphaRange = float2(texInfo.minValue.a, texInfo.maxValue.a);

            // Update base color parameter and texture.
            float4 baseColor = getBaseColor();
            if (isColorConstant)
            {
                baseColor = float4(texInfo.value.rgb, baseColor.a);
                mIsTexturedBaseColorConstant = true;
            }
            if (hasAlpha && isAlphaConstant)
            {
                baseColor = float4(baseColor.rgb, texInfo.value.a);
                mIsTexturedAlphaConstant = true;
            }
            setBaseColor(baseColor);

            if (isColorConstant && (!hasAlpha || isAlphaConstant))
            {
                clearTexture(Material::TextureSlot::BaseColor);
                stats.texturesRemoved[(size_t)slot]++;
            }

            updateBaseColorType();
            updateAlphaMode();

            if (!previouslyOpaque && isOpaque()) stats.disabledAlpha++;

            break;
        }
        case TextureSlot::Specular:
        {
            if (texInfo.isConstant(channelMask))
            {
                clearTexture(Material::TextureSlot::Specular);
                setSpecularParams(texInfo.value);
                stats.texturesRemoved[(size_t)slot]++;
            }
            break;
        }
        case TextureSlot::Emissive:
        {
            if (texInfo.isConstant(channelMask))
            {
                clearTexture(Material::TextureSlot::Emissive);
                setEmissiveColor(texInfo.value.rgb);
                stats.texturesRemoved[(size_t)slot]++;
            }
            break;
        }
        case TextureSlot::Normal:
        {
            // Determine which channels of the normal map are used.
            switch (getNormalMapType())
            {
            case NormalMapRG:
                channelMask = TextureChannelFlags::Red | TextureChannelFlags::Green;
                break;
            case NormalMapRGB:
                channelMask = TextureChannelFlags::RGB;
                break;
            default:
                logWarning("BasicMaterial::optimizeTexture() - Unsupported normal map mode");
                channelMask = TextureChannelFlags::RGBA;
                break;
            }

            if (texInfo.isConstant(channelMask))
            {
                // The Material class doesn't have a way to specify constant value normal map.
                // Count number of constant normal maps and issue a perf warning below instead.
                stats.constantNormalMaps++;
            }
            break;
        }
        case TextureSlot::Transmission:
        {
            if (texInfo.isConstant(channelMask))
            {
                clearTexture(Material::TextureSlot::Transmission);
                setTransmissionColor(texInfo.value.rgb);
                stats.texturesRemoved[(size_t)slot]++;
            }
            break;
        }
        case TextureSlot::Displacement:
        {
            // Nothing to do here, displacement texture is prepared when calling prepareDisplacementMap().
            break;
        }
        default:
            throw std::logic_error("BasicMaterial::optimizeTexture() - Unexpected texture slot: " + std::to_string((uint32_t)slot));
        }
    }

    void BasicMaterial::prepareDisplacementMapForRendering()
    {
        if (isDisplaced())
        {
            // Creates RGBA texture with MIP pyramid containing average, min, max values.
            assert(mResources.displacementMap != nullptr);
            Falcor::ResourceFormat oldFormat = mResources.displacementMap->getFormat();

            // Replace texture with a 4 component one if necessary.
            if (getFormatChannelCount(oldFormat) < 4)
            {
                Falcor::ResourceFormat newFormat = ResourceFormat::RGBA16Float;
                Resource::BindFlags bf = mResources.displacementMap->getBindFlags() | Resource::BindFlags::UnorderedAccess | Resource::BindFlags::RenderTarget;
                Texture::SharedPtr newDisplacementTex = Texture::create2D(mResources.displacementMap->getWidth(), mResources.displacementMap->getHeight(), newFormat, mResources.displacementMap->getArraySize(), Resource::kMaxPossible, nullptr, bf);

                // Copy base level.
                RenderContext* pContext = gpDevice->getRenderContext();
                uint32_t arraySize = mResources.displacementMap->getArraySize();
                for (uint32_t a = 0; a < arraySize; a++)
                {
                    auto srv = mResources.displacementMap->getSRV(0, 1, a, 1);
                    auto rtv = newDisplacementTex->getRTV(0, a, 1);
                    const Sampler::ReductionMode redModes[] = { Sampler::ReductionMode::Standard, Sampler::ReductionMode::Standard, Sampler::ReductionMode::Standard, Sampler::ReductionMode::Standard };
                    const float4 componentsTransform[] = { float4(1.0f, 0.0f, 0.0f, 0.0f), float4(1.0f, 0.0f, 0.0f, 0.0f), float4(1.0f, 0.0f, 0.0f, 0.0f), float4(1.0f, 0.0f, 0.0f, 0.0f) };
                    pContext->blit(srv, rtv, RenderContext::kMaxRect, RenderContext::kMaxRect, Sampler::Filter::Linear, redModes, componentsTransform);
                }

                mResources.displacementMap = newDisplacementTex;
            }

            // Build min/max MIPS.
            mResources.displacementMap->generateMips(gpDevice->getRenderContext(), true);
        }
    }

    void BasicMaterial::loadTexture(TextureSlot slot, const std::string& filename, bool useSrgb)
    {
        if (!hasTextureSlot(slot))
        {
            logWarning("BasicMaterial::loadTexture() - Material '" + getName() + "' does not have texture slot '" + to_string(slot) + "'. Ignoring call.");
            return;
        }

        std::string fullpath;
        if (findFileInDataDirectories(filename, fullpath))
        {
            auto texture = Texture::createFromFile(fullpath, true, useSrgb && getTextureSlotInfo(slot).srgb);
            if (texture)
            {
                setTexture(slot, texture);
                // Flush and sync in order to prevent the upload heap from growing too large. Doing so after
                // every texture creation is overly conservative, and will likely lead to performance issues
                // due to the forced CPU/GPU sync.
                gpDevice->flushAndSync();
            }
        }
    }

    void BasicMaterial::setDisplacementScale(float scale)
    {
        if (mData.displacementScale != scale)
        {
            mData.displacementScale = scale;
            markUpdates(UpdateFlags::DataChanged | UpdateFlags::DisplacementChanged);
        }
    }

    void BasicMaterial::setDisplacementOffset(float offset)
    {
        if (mData.displacementOffset != offset)
        {
            mData.displacementOffset = offset;
            markUpdates(UpdateFlags::DataChanged | UpdateFlags::DisplacementChanged);
        }
    }

    void BasicMaterial::setBaseColor(const float4& color)
    {
        if (mData.baseColor != color)
        {
            mData.baseColor = color;
            markUpdates(UpdateFlags::DataChanged);
            updateBaseColorType();
            updateAlphaMode();
        }
    }

    void BasicMaterial::setSpecularParams(const float4& color)
    {
        if (mData.specular != color)
        {
            mData.specular = color;
            markUpdates(UpdateFlags::DataChanged);
            updateSpecularType();
        }
    }

    void BasicMaterial::setTransmissionColor(const float3& transmissionColor)
    {
        if (mData.transmission != transmissionColor)
        {
            mData.transmission = transmissionColor;
            markUpdates(UpdateFlags::DataChanged);
            updateTransmissionType();
        }
    }

    void BasicMaterial::setDiffuseTransmission(float diffuseTransmission)
    {
        if (mData.diffuseTransmission != diffuseTransmission)
        {
            mData.diffuseTransmission = diffuseTransmission;
            markUpdates(UpdateFlags::DataChanged);
            updateDoubleSidedFlag();
        }
    }

    void BasicMaterial::setSpecularTransmission(float specularTransmission)
    {
        if (mData.specularTransmission != specularTransmission)
        {
            mData.specularTransmission = specularTransmission;
            markUpdates(UpdateFlags::DataChanged);
            updateDoubleSidedFlag();
        }
    }

    void BasicMaterial::setVolumeAbsorption(const float3& volumeAbsorption)
    {
        if (mData.volumeAbsorption != volumeAbsorption)
        {
            mData.volumeAbsorption = volumeAbsorption;
            markUpdates(UpdateFlags::DataChanged);
        }
    }

    void BasicMaterial::setVolumeScattering(const float3& volumeScattering)
    {
        if (mData.volumeScattering != volumeScattering)
        {
            mData.volumeScattering = volumeScattering;
            markUpdates(UpdateFlags::DataChanged);
        }
    }

    void BasicMaterial::setVolumeAnisotropy(float volumeAnisotropy)
    {
        auto clampedAnisotropy = clamp(volumeAnisotropy, -kMaxVolumeAnisotropy, kMaxVolumeAnisotropy);
        if (mData.volumeAnisotropy != clampedAnisotropy)
        {
            mData.volumeAnisotropy = clampedAnisotropy;
            markUpdates(UpdateFlags::DataChanged);
        }
    }

    void BasicMaterial::setEmissiveColor(const float3& color)
    {
        if (mData.emissive != color)
        {
            mData.emissive = color;
            markUpdates(UpdateFlags::DataChanged);
            updateEmissiveType();
        }
    }

    void BasicMaterial::setEmissiveFactor(float factor)
    {
        if (mData.emissiveFactor != factor)
        {
            mData.emissiveFactor = factor;
            markUpdates(UpdateFlags::DataChanged);
            updateEmissiveType();
        }
    }

    bool BasicMaterial::isEqual(const Material::SharedPtr& pOther) const
    {
        auto other = std::dynamic_pointer_cast<BasicMaterial>(pOther);
        if (!other) return false;

        return (*this) == (*other);
    }

    bool BasicMaterial::operator==(const BasicMaterial& other) const
    {
#define compare_field(_a) if (mData._a != other.mData._a) return false
        compare_field(baseColor);
        compare_field(specular);
        compare_field(emissive);
        compare_field(emissiveFactor);
        compare_field(alphaThreshold);
        compare_field(IoR);
        compare_field(diffuseTransmission);
        compare_field(specularTransmission);
        compare_field(transmission);
        compare_field(volumeAbsorption);
        compare_field(volumeAnisotropy);
        compare_field(volumeScattering);
        compare_field(flags);
        compare_field(type);
        compare_field(displacementScale);
        compare_field(displacementOffset);
#undef compare_field

#define compare_texture(_a) if (mResources._a != other.mResources._a) return false
        compare_texture(baseColor);
        compare_texture(specular);
        compare_texture(emissive);
        compare_texture(normalMap);
        compare_texture(transmission);
        compare_texture(displacementMap);
#undef compare_texture

        if (mResources.samplerState != other.mResources.samplerState) return false;
        if (mTextureTransform.getMatrix() != other.mTextureTransform.getMatrix()) return false;

        return true;
    }

    void BasicMaterial::setFlags(uint32_t flags)
    {
        if (mData.flags != flags)
        {
            mData.flags = flags;
            markUpdates(UpdateFlags::DataChanged);
        }
    }

    template<typename vec>
    static uint32_t getChannelMode(bool hasTexture, const vec& color)
    {
        if (hasTexture) return ChannelTypeTexture;
        if (luminance(color) == 0) return ChannelTypeUnused;
        return ChannelTypeConst;
    }

    void BasicMaterial::updateBaseColorType()
    {
        bool useTexture = mResources.baseColor != nullptr && !mIsTexturedBaseColorConstant;
        setFlags(PACK_BASE_COLOR_TYPE(mData.flags, getChannelMode(useTexture, mData.baseColor.rgb)));
    }

    void BasicMaterial::updateSpecularType()
    {
        setFlags(PACK_SPECULAR_TYPE(mData.flags, getChannelMode(mResources.specular != nullptr, mData.specular)));
    }

    void BasicMaterial::updateEmissiveType()
    {
        setFlags(PACK_EMISSIVE_TYPE(mData.flags, getChannelMode(mResources.emissive != nullptr, mData.emissive * mData.emissiveFactor)));
    }

    void BasicMaterial::updateTransmissionType()
    {
        setFlags(PACK_TRANS_TYPE(mData.flags, getChannelMode(mResources.transmission != nullptr, mData.transmission)));
    }

    void BasicMaterial::updateAlphaMode()
    {
        // Decide how alpha channel should be accessed.
        bool hasAlpha = mResources.baseColor && doesFormatHasAlpha(mResources.baseColor->getFormat());
        bool useTexture = hasAlpha && !mIsTexturedAlphaConstant;
        setFlags(PACK_ALPHA_TYPE(mData.flags, getChannelMode(useTexture, mData.baseColor.a)));

        // Set alpha range to the fixed alpha value if non-textured.
        if (!hasAlpha) mAlphaRange = float2(mData.baseColor.a);

        // Decide if we need to run the alpha test.
        // This is derived from the current alpha threshold and conservative alpha range.
        // If the test will never fail we disable it. This optimization assumes basic alpha thresholding.
        // TODO: Update the logic if other alpha modes are added.
        bool useAlpha = mAlphaRange.x < mData.alphaThreshold;
        setAlphaMode(useAlpha ? AlphaModeMask : AlphaModeOpaque);
    }

    void BasicMaterial::updateNormalMapMode()
    {
        uint32_t normalMode = NormalMapUnused;
        if (mResources.normalMap)
        {
            switch(getFormatChannelCount(mResources.normalMap->getFormat()))
            {
            case 2:
                normalMode = NormalMapRG;
                break;
            case 3:
            case 4: // Some texture formats don't support RGB, only RGBA. We have no use for the alpha channel in the normal map.
                normalMode = NormalMapRGB;
                break;
            default:
                should_not_get_here();
                logWarning("Unsupported normal map format for material " + mName);
            }
        }
        setFlags(PACK_NORMAL_MAP_TYPE(mData.flags, normalMode));
    }

    void BasicMaterial::updateDoubleSidedFlag()
    {
        bool doubleSided = mDoubleSided;
        // Make double sided if diffuse or specular transmission is used.
        if (mData.diffuseTransmission > 0.f || mData.specularTransmission > 0.f) doubleSided = true;
        // Make double sided if a dispacement map is used since backfacing surfaces can become frontfacing.
        if (mResources.displacementMap != nullptr) doubleSided = true;
        setFlags(PACK_DOUBLE_SIDED(mData.flags, doubleSided ? 1 : 0));
    }

    void BasicMaterial::updateDisplacementFlag()
    {
        bool hasMap = (mResources.displacementMap != nullptr);
        setFlags(PACK_DISPLACEMENT_MAP(mData.flags, hasMap ? 1 : 0));
    }

    SCRIPT_BINDING(BasicMaterial)
    {
        SCRIPT_BINDING_DEPENDENCY(Material)

        pybind11::class_<BasicMaterial, Material, BasicMaterial::SharedPtr> material(m, "BasicMaterial");
        material.def_property("baseColor", &BasicMaterial::getBaseColor, &BasicMaterial::setBaseColor);
        material.def_property("specularParams", &BasicMaterial::getSpecularParams, &BasicMaterial::setSpecularParams);
        material.def_property("transmissionColor", &BasicMaterial::getTransmissionColor, &BasicMaterial::setTransmissionColor);
        material.def_property("diffuseTransmission", &BasicMaterial::getDiffuseTransmission, &BasicMaterial::setDiffuseTransmission);
        material.def_property("specularTransmission", &BasicMaterial::getSpecularTransmission, &BasicMaterial::setSpecularTransmission);
        material.def_property("volumeAbsorption", &BasicMaterial::getVolumeAbsorption, &BasicMaterial::setVolumeAbsorption);
        material.def_property("volumeScattering", &BasicMaterial::getVolumeScattering, &BasicMaterial::setVolumeScattering);
        material.def_property("volumeAnisotropy", &BasicMaterial::getVolumeAnisotropy, &BasicMaterial::setVolumeAnisotropy);
        material.def_property("indexOfRefraction", &BasicMaterial::getIndexOfRefraction, &BasicMaterial::setIndexOfRefraction);
        material.def_property("emissiveColor", &BasicMaterial::getEmissiveColor, &BasicMaterial::setEmissiveColor);
        material.def_property("emissiveFactor", &BasicMaterial::getEmissiveFactor, &BasicMaterial::setEmissiveFactor);
        material.def_property("alphaMode", &BasicMaterial::getAlphaMode, &BasicMaterial::setAlphaMode);
        material.def_property("alphaThreshold", &BasicMaterial::getAlphaThreshold, &BasicMaterial::setAlphaThreshold);
        material.def_property("doubleSided", &BasicMaterial::isDoubleSided, &BasicMaterial::setDoubleSided);
        material.def_property("nestedPriority", &BasicMaterial::getNestedPriority, &BasicMaterial::setNestedPriority);
        material.def_property("thinSurface", &BasicMaterial::isThinSurface, &BasicMaterial::setThinSurface);
        material.def_property("displacementScale", &BasicMaterial::getDisplacementScale, &BasicMaterial::setDisplacementScale);
        material.def_property("displacementOffset", &BasicMaterial::getDisplacementOffset, &BasicMaterial::setDisplacementOffset);
    }
}
