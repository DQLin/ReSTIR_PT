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
#include "MaterialData.slang"
#include "Scene/Transform.h"
#include "Utils/Image/TextureAnalyzer.h"

namespace Falcor
{
    class BasicMaterial;

    /** Abstract base class for materials.
    */
    class dlldecl Material : public std::enable_shared_from_this<Material>
    {
    public:
        // While this is an abstract base class, we still need a holder type (shared_ptr)
        // for pybind11 bindings to work on inherited types.
        using SharedPtr = std::shared_ptr<Material>;

        /** Flags indicating if and what was updated in the material.
        */
        enum class UpdateFlags
        {
            None                = 0x0,  ///< Nothing updated.
            DataChanged         = 0x1,  ///< Material data (parameters) changed.
            ResourcesChanged    = 0x2,  ///< Material resources (textures, samplers) changed.
            DisplacementChanged = 0x4,  ///< Displacement mapping parameters changed (only for materials that support displacement).
        };

        /** Texture slots available for use.
            A material does not need to expose/bind all slots.
        */
        enum class TextureSlot
        {
            BaseColor,
            Specular,
            Emissive,
            Normal,
            Transmission,
            Displacement,

            Count // Must be last
        };

        struct TextureSlotInfo
        {
            std::string         name;                               ///< Name of texture slot.
            TextureChannelFlags mask = TextureChannelFlags::None;   ///< Mask of enabled texture channels.
            bool                srgb = false;                       ///< True if texture should be loaded in sRGB space.

            bool isEnabled() const { return mask != TextureChannelFlags::None; }
        };

        struct TextureOptimizationStats
        {
            std::array<size_t, (size_t)TextureSlot::Count> texturesRemoved = {};
            size_t disabledAlpha = 0;
            size_t constantNormalMaps = 0;
        };

        virtual ~Material() = default;

        /** Render the UI.
            \return True if the material was modified.
        */
        virtual bool renderUI(Gui::Widgets& widget);

        /** Get the material type.
        */
        virtual MaterialType getType() const = 0;

        /** Returns true if the material is opaque.
        */
        virtual bool isOpaque() const { return true; }

        /** Returns true if the material has a displacement map.
        */
        virtual bool isDisplaced() const { return false; }

        /** Returns true if the material is emissive.
        */
        virtual bool isEmissive() const { return false; }

        /** Compares material to another material.
            \param[in] pOther Other material.
            \return true if all materials properties *except* the name are identical.
        */
        virtual bool isEqual(const Material::SharedPtr& pOther) const = 0;

        /** Get information about a texture slot.
            \param[in] slot The texture slot.
            \return Info about the slot. If the slot doesn't exist isEnabled() returns false.
        */
        virtual const TextureSlotInfo& getTextureSlotInfo(const TextureSlot slot) const { return sUnusedTextureSlotInfo; }

        /** Check if material has a given texture slot.
            \param[in] slot The texture slot.
            \return True if the texture slot exists. Use getTexture() to check if a texture is bound.
        */
        virtual bool hasTextureSlot(const TextureSlot slot) const { return getTextureSlotInfo(slot).isEnabled(); }

        /** Set one of the available texture slots.
            The call is ignored if the slot doesn't exist.
            \param[in] slot The texture slot.
            \param[in] pTexture The texture.
        */
        virtual void setTexture(const TextureSlot slot, const Texture::SharedPtr& pTexture);

        /** Load one of the available texture slots.
            The call is ignored if the slot doesn't exist.
            \param[in] The texture slot.
            \param[in] filename Filename to load texture from.
            \param[in] useSrgb Load texture as sRGB format.
        */
        virtual void loadTexture(const TextureSlot slot, const std::string& filename, bool useSrgb = true);

        /** Clear one of the available texture slots.
            The call is ignored if the slot doesn't exist.
            \param[in] The texture slot.
        */
        virtual void clearTexture(const TextureSlot slot) { setTexture(slot, nullptr); }

        /** Get one of the available texture slots.
            \param[in] The texture slot.
            \return Texture object if bound, or nullptr if unbound or slot doesn't exist.
        */
        virtual Texture::SharedPtr getTexture(const TextureSlot slot) const { return nullptr; }

        /** Optimize texture usage for the given texture slot.
            This function may replace constant textures by uniform material parameters etc.
            \param[in] slot The texture slot.
            \param[in] texInfo Information about the texture bound to this slot.
            \param[out] stats Optimization stats passed back to the caller.
        */
        virtual void optimizeTexture(const TextureSlot slot, const TextureAnalyzer::Result& texInfo, TextureOptimizationStats& stats) {}

        /** If material is displaced, prepares the displacement map in order to match the format required for rendering.
        */
        virtual void prepareDisplacementMapForRendering() {}

        /** Return the maximum dimensions of the bound textures.
        */
        virtual uint2 getMaxTextureDimensions() const;

        /** Bind a default texture sampler to the material.
        */
        virtual void setDefaultTextureSampler(const Sampler::SharedPtr& pSampler) {}

        /** Get the default texture sampler attached to the material.
        */
        virtual Sampler::SharedPtr getDefaultTextureSampler() const { return nullptr; }

        /** Set the material name.
        */
        void setName(const std::string& name) { mName = name; }

        /** Get the material name.
        */
        const std::string& getName() const { return mName; }

        /** Set the material texture transform.
        */
        void setTextureTransform(const Transform& texTransform);

        /** Get a reference to the material texture transform.
        */
        Transform& getTextureTransform() { return mTextureTransform; }

        /** Get the material texture transform.
        */
        const Transform& getTextureTransform() const { return mTextureTransform; }

        /** Returns the updates since the last call to clearUpdates.
        */
        UpdateFlags getUpdates() const { return mUpdates; }

        /** Clears the updates.
        */
        void clearUpdates() { mUpdates = UpdateFlags::None; }

        /** Returns the global updates (across all materials) since the last call to clearGlobalUpdates.
        */
        static UpdateFlags getGlobalUpdates() { return sGlobalUpdates; }

        /** Clears the global updates.
        */
        static void clearGlobalUpdates() { sGlobalUpdates = UpdateFlags::None; }

        // Temporary convenience function to downcast Material to BasicMaterial.
        // This is because a large portion of the interface hasn't been ported to the Material base class yet.
        // TODO: Remove this helper later
        std::shared_ptr<BasicMaterial> toBasicMaterial()
        {
            switch (getType())
            {
            case MaterialType::Standard:
            case MaterialType::Hair:
            case MaterialType::Cloth:
                assert(std::dynamic_pointer_cast<BasicMaterial>(shared_from_this()));
                return std::static_pointer_cast<BasicMaterial>(shared_from_this());
            default:
                return nullptr;
            }
        }

    protected:
        Material(const std::string& name);

        void markUpdates(UpdateFlags updates);

        std::string mName;                          ///< Name of the material.
        Transform mTextureTransform;                ///< Texture transform. This is currently applied at load time by pre-transforming the texture coordinates.

        mutable UpdateFlags mUpdates = UpdateFlags::None;
        static UpdateFlags sGlobalUpdates;

        static const TextureSlotInfo sUnusedTextureSlotInfo;

        friend class SceneCache;
    };

    inline std::string to_string(MaterialType type)
    {
        switch (type)
        {
#define tostr(t_) case MaterialType::t_: return #t_;
            tostr(Standard);
            tostr(Cloth);
            tostr(Hair);
#undef tostr
        default:
            throw std::runtime_error("Invalid material type");
        }
    }

    inline std::string to_string(Material::TextureSlot slot)
    {
        switch (slot)
        {
#define tostr(a) case Material::TextureSlot::a: return #a;
            tostr(BaseColor);
            tostr(Specular);
            tostr(Emissive);
            tostr(Normal);
            tostr(Transmission);
            tostr(Displacement);
#undef tostr
        default:
            throw std::runtime_error("Invalid texture slot");
        }
    }

    enum_class_operators(Material::UpdateFlags);
}
