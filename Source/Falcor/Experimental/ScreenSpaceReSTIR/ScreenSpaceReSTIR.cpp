/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/
#include "ScreenSpaceReSTIR.h"
#include "Utils/Color/ColorHelpers.slang"

namespace Falcor
{
    namespace
    {
        const char kReflectTypesFile[] = "Experimental/ScreenSpaceReSTIR/ReflectTypes.cs.slang";
        const char kUpdateEmissiveTriangles[] = "Experimental/ScreenSpaceReSTIR/UpdateEmissiveTriangles.cs.slang";
        const char kGenerateLightTilesFile[] = "Experimental/ScreenSpaceReSTIR/GenerateLightTiles.cs.slang";
        const char kInitialResamplingFile[] = "Experimental/ScreenSpaceReSTIR/InitialResampling.cs.slang";
        const char kTemporalResamplingFile[] = "Experimental/ScreenSpaceReSTIR/TemporalResampling.cs.slang";
        const char kSpatialResamplingFile[] = "Experimental/ScreenSpaceReSTIR/SpatialResampling.cs.slang";
        const char kEvaluateFinalSamplesFile[] = "Experimental/ScreenSpaceReSTIR/EvaluateFinalSamples.cs.slang";
        const char kGIResamplingFile[] = "Experimental/ScreenSpaceReSTIR/GIResampling.cs.slang";
        const char kGIClearReservoirsFile[] = "Experimental/ScreenSpaceReSTIR/GIClearReservoirs.cs.slang";

        const std::string kShaderModel = "6_5";

        const Gui::DropdownList kDebugOutputList =
        {
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::Disabled, "Disabled" },
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::Position, "Position" },
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::Depth, "Depth" },
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::Normal, "Normal" },
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::FaceNormal, "FaceNormal" },
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::DiffuseWeight, "DiffuseWeight" },
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::SpecularWeight, "SpecularWeight" },
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::SpecularRoughness, "SpecularRoughness" },
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::PackedNormal, "PackedNormal" },
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::PackedDepth, "PackedDepth" },
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::InitialWeight, "InitialWeight" },
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::TemporalReuse, "TemporalReuse" },
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::SpatialReuse, "SpatialReuse" },
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::FinalSampleDir, "FinalSampleDir" },
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::FinalSampleDistance, "FinalSampleDistance" },
            { (uint32_t)ScreenSpaceReSTIR::DebugOutput::FinalSampleLi, "FinalSampleLi" },
        };

        const Gui::DropdownList kReSTIRModeList =
        {
            { (uint32_t)ScreenSpaceReSTIR::ReSTIRMode::InputOnly, "Input Only" },
            { (uint32_t)ScreenSpaceReSTIR::ReSTIRMode::TemporalOnly, "Temporal Reuse Only" },
            { (uint32_t)ScreenSpaceReSTIR::ReSTIRMode::TemporalAndBiasedSpatial, "Temporal + Biased Spatial Reuse" },
            { (uint32_t)ScreenSpaceReSTIR::ReSTIRMode::TemporalAndUnbiasedSpatial, "Temporal + Unbiased Spatial Reuse" },
        };

        const Gui::DropdownList kTargetPdfList =
        {
            { (uint32_t)ScreenSpaceReSTIR::TargetPDF::IncomingRadiance, "Incoming Radiance" },
            { (uint32_t)ScreenSpaceReSTIR::TargetPDF::OutgoingRadiance, "Outgoing Radiance" },
        };

        const Gui::DropdownList kSpatialReusePatternList =
        {
            { (uint32_t)SpatialReusePattern::Default, std::string("Default")},
        };

        const uint32_t kNeighborOffsetCount = 8192;
    }

    ScreenSpaceReSTIR::SharedPtr ScreenSpaceReSTIR::create(const Scene::SharedPtr& pScene, const Options::SharedPtr& options, int numReSTIRInstances, int ReSTIRInstanceID)
    {
        return SharedPtr(new ScreenSpaceReSTIR(pScene, options, numReSTIRInstances, ReSTIRInstanceID));
    }

    ScreenSpaceReSTIR::ScreenSpaceReSTIR(const Scene::SharedPtr& pScene, const Options::SharedPtr& options, int numReSTIRInstances, int ReSTIRInstanceID)
        : mpScene(pScene)
        , mOptions(options)
    {
        assert(mpScene);

        mpPixelDebug = PixelDebug::create();

        // Create compute pass for reflecting data types.
        Program::Desc desc;
        Program::DefineList defines;
        defines.add(mpScene->getSceneDefines());
        defines.add(getLightsDefines());
        desc.addShaderLibrary(kReflectTypesFile).csEntry("main").setShaderModel(kShaderModel);
        mpReflectTypes = ComputePass::create(desc, defines);

        // Create neighbor offset texture.
        mpNeighborOffsets = createNeighborOffsetTexture(kNeighborOffsetCount);

        mNumReSTIRInstances = numReSTIRInstances;
        mReSTIRInstanceIndex = ReSTIRInstanceID;
        mFrameIndex = mReSTIRInstanceIndex;
    }

    Program::DefineList ScreenSpaceReSTIR::getDefines() const
    {
        Program::DefineList defines;
        defines.add("SCREEN_SPACE_RESTIR_USE_DI", mOptions->useReSTIRDI ? "1" : "0");
        defines.add("SCREEN_SPACE_RESTIR_USE_GI", mOptions->useReSTIRGI ? "1" : "0");
        defines.add("SCREEN_SPACE_RESTIR_GI_DIFFUSE_THRESHOLD", std::to_string(mOptions->diffuseThreshold));
        defines.add("RESTIR_GI_USE_RESTIR_N", mOptions->reSTIRGIUseReSTIRN ? "1" : "0");
        return defines;
    }

    void ScreenSpaceReSTIR::setShaderData(const ShaderVar& var) const
    {
        var["surfaceData"] = mpSurfaceData;
        var["normalDepth"] = mpNormalDepthTexture;
        var["finalSamples"] = mpFinalSamples;

        var["frameDim"] = mFrameDim;

        // ReSTIR GI.
        var["initialSamples"] = mpGIInitialSamples;
        //var["prevReservoirs"] = mpGIReservoirs[(mFrameIndex + 0) % 2];
        //var["reservoirs"] = mpGIReservoirs[(mFrameIndex + 1) % 2];
        var["prevReservoirs"] = mpGIReservoirs[((mFrameIndex - mReSTIRInstanceIndex) / mNumReSTIRInstances + 0) % 2];
        var["reservoirs"] = mpGIReservoirs[((mFrameIndex - mReSTIRInstanceIndex) / mNumReSTIRInstances + 1) % 2];

        var["giReservoirCount"] = mOptions->reSTIRGIReservoirCount;
    }

    void ScreenSpaceReSTIR::enablePass(bool enabled)
    {
        mOptions->enabled = enabled;
    }

    bool ScreenSpaceReSTIR::renderUI(Gui::Widgets& widget)
    {
        if (mReSTIRInstanceIndex != 0) return false;

        bool dirty = false;

        dirty = widget.checkbox("Enable Pass", mOptions->enabled);

        mRecompile |= widget.checkbox("Use ReSTIR DI", mOptions->useReSTIRDI);
        widget.tooltip("Enable ReSTIR for direct illumination.");

        mRecompile |= widget.checkbox("Use ReSTIR GI", mOptions->useReSTIRGI);
        widget.tooltip("Enable ReSTIR for indirect illumination.");

        if (auto group = widget.group("Debugging"))
        {
            mRecompile |= group.dropdown("Debug output", kDebugOutputList, reinterpret_cast<uint32_t&>(mOptions->debugOutput));
            mpPixelDebug->renderUI(group);

        }

        if (auto group = widget.group("Common options"))
        {
            dirty |= widget.var("Normal threshold", mOptions->normalThreshold, 0.f, 1.f);
            widget.tooltip("Normal cosine threshold for reusing temporal samples or spatial neighbor samples.");

            dirty |= widget.var("Depth threshold", mOptions->depthThreshold, 0.f, 1.f);
            widget.tooltip("Relative depth threshold for reusing temporal samples or spatial neighbor samples.");
        }

        if (auto groupDI = widget.group("ReSTIR DI options"))
        {
            if (auto group = widget.group("Light selection weights"))
            {
                mRecompile |= group.var("Environment", mOptions->envLightWeight, 0.f, 1.f);
                group.tooltip("Relative weight for selecting the env map when sampling a light.");

                mRecompile |= group.var("Emissive", mOptions->emissiveLightWeight, 0.f, 1.f);
                group.tooltip("Relative weight for selecting an emissive light when sampling a light.");

                mRecompile |= group.var("Analytic", mOptions->analyticLightWeight, 0.f, 1.f);
                group.tooltip("Relative weight for selecting an analytical light when sampling a light.");
            }

            if (auto group = widget.group("Emissive lights"))
            {
                mRecompile |= group.checkbox("Use emissive texture for sampling", mOptions->useEmissiveTextureForSampling);
                group.tooltip("Use emissive texture for light sample evaluation.");

                mRecompile |= group.checkbox("Use emissive texture for shading", mOptions->useEmissiveTextureForShading);
                group.tooltip("Use emissive texture for shading.");

                mRecompile |= group.checkbox("Use local emissive triangles", mOptions->useLocalEmissiveTriangles);
                group.tooltip("Use local emissive triangle data structure (for more efficient sampling/evaluation).");
            }

            if (auto group = widget.group("Light tiles"))
            {
                mRecompile |= group.var("Tile count", mOptions->lightTileCount, 1u, 1024u);
                group.tooltip("Number of light tiles to compute.");

                mRecompile |= group.var("Tile size", mOptions->lightTileSize, 1u, 8192u);
                group.tooltip("Number of lights per light tile.");
            }

            if (auto group = widget.group("Visibility", true))
            {
                mRecompile |= group.checkbox("Use alpha test", mOptions->useAlphaTest);
                group.tooltip("Use alpha testing on non-opaque triangles.");

                mRecompile |= group.checkbox("Use initial visibility", mOptions->useInitialVisibility);
                group.tooltip("Check visibility on inital sample.");

                mRecompile |= group.checkbox("Use final visibility", mOptions->useFinalVisibility);
                group.tooltip("Check visibility on final sample.");

                if (mOptions->useFinalVisibility)
                {
                    mRecompile |= group.checkbox("Reuse final visibility", mOptions->reuseFinalVisibility);
                    group.tooltip("Reuse final visibility temporally.");
                }
            }

            if (auto group = widget.group("Initial resampling", true))
            {
                mRecompile |= group.var("Screen tile size", mOptions->screenTileSize, 1u, 128u);
                group.tooltip("Size of screen tile that samples from the same light tile.");

                mRecompile |= group.var("Initial light sample count", mOptions->initialLightSampleCount, 1u, 1024u);
                group.tooltip("Number of initial light samples to resample per pixel.");

                mRecompile |= group.var("Initial BRDF sample count", mOptions->initialBRDFSampleCount, 0u, 16u);
                group.tooltip("Number of initial BRDF samples to resample per pixel.");

                dirty |= group.var("BRDF Cutoff", mOptions->brdfCutoff, 0.f, 1.f);
                group.tooltip("Value in range [0,1] to determine how much to shorten BRDF rays.");
            }

            if (auto group = widget.group("Temporal resampling", true))
            {
                dirty |= group.checkbox("Use temporal resampling", mOptions->useTemporalResampling);

                mRecompile |= group.var("Max history length", mOptions->maxHistoryLength, 0u, 100u);
                group.tooltip("Maximum temporal history length.");
            }

            if (auto group = widget.group("Spatial resampling", true))
            {
                dirty |= group.checkbox("Use spatial resampling", mOptions->useSpatialResampling);

                dirty |= group.var("Iterations", mOptions->spatialIterations, 0u, 8u);
                group.tooltip("Number of spatial resampling iterations.");

                dirty |= group.var("Neighbor count", mOptions->spatialNeighborCount, 0u, 32u);
                group.tooltip("Number of neighbor samples to resample per pixel and iteration.");

                dirty |= group.var("Gather radius", mOptions->spatialGatherRadius, 5u, 40u);
                group.tooltip("Radius to gather samples from.");
            }

            mRecompile |= widget.checkbox("Use pairwise MIS", mOptions->usePairwiseMIS);
            widget.tooltip("Use pairwise MIS when combining samples.");

            mRecompile |= widget.checkbox("Unbiased", mOptions->unbiased);
            widget.tooltip("Use unbiased version of ReSTIR by querying extra visibility rays.");
        }

        if (auto group = widget.group("ReSTIR GI options"))
        {
            mRecompile |= group.dropdown("Mode", kReSTIRModeList, reinterpret_cast<uint32_t&>(mOptions->reSTIRMode));
            mRecompile |= group.dropdown("Target PDF", kTargetPdfList, reinterpret_cast<uint32_t&>(mOptions->targetPdf));

            dirty |= group.var("Max Temporal Samples", mOptions->reSTIRGITemporalMaxSamples, 0u, 1000u);
            group.tooltip("Maximum number of temporal samples.");

            dirty |= group.var("Max Spatial Samples", mOptions->reSTIRGISpatialMaxSamples, 0u, 5000u);
            group.tooltip("Maximum number of temporal samples.");

            dirty |= group.var("Reservoir Count", mOptions->reSTIRGIReservoirCount, 1u, 32u);
            group.tooltip("Number of reservoirs per pixel.");

            dirty |= group.checkbox("use ReSTIR N", mOptions->reSTIRGIUseReSTIRN);
            group.tooltip("Try to execute ReSTIR GI N times (with a new initial samples for each time).");

            dirty |= group.var("Max Sample Age", mOptions->reSTIRGIMaxSampleAge, 3u, 1000u);
            group.tooltip("Maximum frames that a sample can survive.");

            dirty |= group.checkbox("Enable Spatial Weight Clamping", mOptions->reSTIRGIEnableSpatialWeightClamping);
            dirty |= group.var("Spatial Weight Clamp Threshold", mOptions->reSTIRGISpatialWeightClampThreshold, 1.f, 1000.f);

            dirty |= group.checkbox("Enable Jacobian Clamping", mOptions->reSTIRGIEnableJacobianClamping);
            dirty |= group.var("Jacobian Clamp Threshold", mOptions->reSTIRGIJacobianClampTreshold, 1.f, 1000.f);

            dirty |= group.checkbox("Enable Temporal Jacobian", mOptions->reSTIREnableTemporalJacobian);

            mRecompile |= group.var("Diffuse Threshold", mOptions->diffuseThreshold, 0.f, 1.f);
            group.tooltip("Do not use ReSTIR GI on pixels whose diffuse component lower than this value.");

            dirty |= group.checkbox("Force Clear Reservoirs", mOptions->forceClearReservoirs);
            group.tooltip("Force clear reservoirs.");
        }

        mRecompile |= mRequestReallocate;
        dirty |= mRecompile;

        if (mRequestReallocate) mRequestParentRecompile = true;

        return dirty;
    }

    //void ScreenSpaceReSTIR::setOptions(const Options& options)
    //{
    //    if (std::memcmp(&options, &mOptions, sizeof(Options)) != 0)
    //    {
    //        mOptions = options;
    //        mRecompile = true;
    //    }
    //}

    void ScreenSpaceReSTIR::beginFrame(RenderContext* pRenderContext, const uint2& frameDim)
    {
        if (mOptions->enabled)
        {
            mFrameDim = frameDim;

            prepareResources(pRenderContext);

            mpPixelDebug->beginFrame(pRenderContext, mFrameDim);
        }
    }

    void ScreenSpaceReSTIR::endFrame(RenderContext* pRenderContext)
    {
        if (mOptions->enabled)
        {
            //mFrameIndex++;
            mFrameIndex += mNumReSTIRInstances;

            // Swap surface data.
            std::swap(mpSurfaceData, mpPrevSurfaceData);

            // Swap reservoirs.
            std::swap(mpReservoirs, mpPrevReservoirs);

            mpPixelDebug->endFrame(pRenderContext);
        }
    }

    void ScreenSpaceReSTIR::updateReSTIRDI(RenderContext* pRenderContext, const Texture::SharedPtr& pMotionVectors)
    {
        PROFILE("ScreenSpaceReSTIR::updateReSTIRDI");

        if (mOptions->enabled)
        {
            mTotalRISPasses = 2 /*tile generation*/ + 1 /*initial resampling*/ + 1 /*temporal*/ + mOptions->spatialIterations /*spatial*/;
            mCurRISPass = 0;
            prepareLighting(pRenderContext);
            updatePrograms();

            if (mOptions->useReSTIRGI)
            {
                reSTIRGIClearPass(pRenderContext);
            }

            if (!mOptions->useReSTIRDI) return;
            updateEmissiveTriangles(pRenderContext);
            generateLightTiles(pRenderContext);
            initialResampling(pRenderContext);
            temporalResampling(pRenderContext, pMotionVectors);
            spatialResampling(pRenderContext);
            evaluateFinalSamples(pRenderContext);
        }
        else
        {
            mRecompile = false;
        }
    }

    void ScreenSpaceReSTIR::updateReSTIRGI(RenderContext* pRenderContext, const Texture::SharedPtr& pMotionVectors)
    {
        if (!mOptions->useReSTIRGI) return;

        PROFILE("ScreenSpaceReSTIR::updateReSTIRGI");
        if (mOptions->enabled)
        {
            auto rootVar = mpGIResampling->getRootVar();
            mpPixelDebug->prepareProgram(mpGIResampling->getProgram(), rootVar);

            mpScene->setRaytracingShaderData(pRenderContext, rootVar);

            auto var = rootVar["CB"]["gGIResampling"];
            var["neighborOffsets"] = mpNeighborOffsets;
            var["frameDim"] = mFrameDim;
            var["frameIndex"] = mFrameIndex;
            var["surfaceData"] = mpSurfaceData;
            var["prevSurfaceData"] = mpPrevSurfaceData;
            var["motionVectors"] = pMotionVectors;
            var["normalDepth"] = mpNormalDepthTexture;
            var["prevNormalDepth"] = mpPrevNormalDepthTexture;
            var["temporalMaxSamples"] = mOptions->reSTIRGITemporalMaxSamples;
            var["spatialMaxSamples"] = mOptions->reSTIRGISpatialMaxSamples;
            var["reservoirCount"] = mOptions->reSTIRGIReservoirCount;
            var["maxSampleAge"] = mOptions->reSTIRGIMaxSampleAge;
            var["cameraOrigin"] = mpScene->getCamera()->getPosition();
            var["prevCameraOrigin"] = mPrevCameraOrigin;
            var["viewProj"] = mpScene->getCamera()->getViewProjMatrixNoJitter();
            var["prevViewProj"] = mPrevViewProj;
            var["forceClearReservoirs"] = mOptions->forceClearReservoirs;
            var["normalThreshold"] = mOptions->normalThreshold;
            var["depthThreshold"] = mOptions->depthThreshold;
            var["initialSamples"] = mpGIInitialSamples;
            var["spatialWeightClampThreshold"] = mOptions->reSTIRGISpatialWeightClampThreshold;
            var["enableSpatialWeightClamping"] = mOptions->reSTIRGIEnableSpatialWeightClamping;
            var["jacobianClampThreshold"] = mOptions->reSTIRGIJacobianClampTreshold;
            var["enableJacobianClamping"] = mOptions->reSTIRGIEnableJacobianClamping;
            var["enableTemporalJacobian"] = mOptions->reSTIREnableTemporalJacobian;

            //var["prevReservoirs"] = mpGIReservoirs[(mFrameIndex + 0) % 2];
            //var["reservoirs"] = mpGIReservoirs[(mFrameIndex + 1) % 2];
            var["prevReservoirs"] = mpGIReservoirs[((mFrameIndex - mReSTIRInstanceIndex) / mNumReSTIRInstances + 0) % 2];
            var["reservoirs"] = mpGIReservoirs[((mFrameIndex - mReSTIRInstanceIndex) / mNumReSTIRInstances + 1) % 2];

            mpGIResampling->execute(pRenderContext, mFrameDim.x, mFrameDim.y, 1);

            pRenderContext->blit(mpNormalDepthTexture->getSRV(), mpPrevNormalDepthTexture->getRTV());

            mPrevCameraOrigin = mpScene->getCamera()->getPosition();
            mPrevViewProj = mpScene->getCamera()->getViewProjMatrixNoJitter();
        }
    }

    void ScreenSpaceReSTIR::prepareResources(RenderContext* pRenderContext)
    {
        // Create light tile buffers.
        {
            uint32_t elementCount = mOptions->lightTileCount * mOptions->lightTileSize;
            if (!mpLightTileData || mpLightTileData->getElementCount() < elementCount)
            {
                mpLightTileData = Buffer::createStructured(mpReflectTypes["lightTileData"], elementCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
            }
        }

        // Create screen sized buffers.
        {
            uint32_t elementCount = mFrameDim.x * mFrameDim.y;
            if (!mpSurfaceData || mpSurfaceData->getElementCount() < elementCount)
            {
                mpSurfaceData = Buffer::createStructured(mpReflectTypes["surfaceData"], elementCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
            }
            if (!mpPrevSurfaceData || mpPrevSurfaceData->getElementCount() < elementCount)
            {
                mpPrevSurfaceData = Buffer::createStructured(mpReflectTypes["surfaceData"], elementCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
            }
            if (!mpReservoirs || mpReservoirs->getElementCount() < elementCount || mRequestReallocate)
            {
                //mpReservoirs = Buffer::createStructured(mpReflectTypes["reservoirs"], elementCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
                // WARNING: this assumes we use the uint4 packedReservoir by default (Reservoir.slang)
                mpReservoirs = Buffer::createStructured(16, elementCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
            }
            if (!mpPrevReservoirs || mpPrevReservoirs->getElementCount() < elementCount || mRequestReallocate)
            {
                //mpPrevReservoirs = Buffer::createStructured(mpReflectTypes["reservoirs"], elementCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
                // WARNING: this assumes we use the uint4 packedReservoir by default (Reservoir.slang)
                mpPrevReservoirs = Buffer::createStructured(16, elementCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
            }

            if (!mpFinalSamples || mpFinalSamples->getElementCount() < elementCount || mRequestReallocate)
            {
                //mpFinalSamples = Buffer::createStructured(mpReflectTypes["finalSamples"], elementCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
                mpFinalSamples = Buffer::createStructured(32, elementCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
            }

            mRequestReallocate = false;

            int initialReservoirCount = mOptions->reSTIRGIUseReSTIRN ? elementCount * mOptions->reSTIRGIReservoirCount : elementCount;

            if (!mpGIInitialSamples || initialReservoirCount != mpGIInitialSamples->getElementCount())
            {
                mpGIInitialSamples = Buffer::createStructured(mpReflectTypes["giReservoirs"], initialReservoirCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
                if (mpGIInitialSamples->getStructSize() % 16 != 0) logWarning("PackedGIReservoir struct size is not a multiple of 16B");
            }

            int reservoirCount = elementCount * 2 * mOptions->reSTIRGIReservoirCount;

            for (int i = 0; i < 2; i++)
            {
                if (!mpGIReservoirs[i] || mpGIReservoirs[i]->getElementCount() != reservoirCount)
                {
                    mpGIReservoirs[i] = Buffer::createStructured(mpReflectTypes["giReservoirs"], reservoirCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
                    if (mpGIReservoirs[i]->getStructSize() % 16 != 0) logWarning("PackedGIReservoir struct size is not a multiple of 16B");
                }
            }
        }

        // Create normal/depth texture.
        if (!mpNormalDepthTexture || mpNormalDepthTexture->getWidth() != mFrameDim.x || mpNormalDepthTexture->getHeight() != mFrameDim.y)
        {
            mpNormalDepthTexture = Texture::create2D(mFrameDim.x, mFrameDim.y, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
        }

        if (!mpPrevNormalDepthTexture || mpPrevNormalDepthTexture->getWidth() != mFrameDim.x || mpPrevNormalDepthTexture->getHeight() != mFrameDim.y)
        {
            mpPrevNormalDepthTexture = Texture::create2D(mFrameDim.x, mFrameDim.y, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess | ResourceBindFlags::RenderTarget);
        }

        // Create debug texture.
        if (!mpDebugOutputTexture || mpDebugOutputTexture->getWidth() != mFrameDim.x || mpDebugOutputTexture->getHeight() != mFrameDim.y)
        {
            mpDebugOutputTexture = Texture::create2D(mFrameDim.x, mFrameDim.y, ResourceFormat::RGBA32Float, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
        }
    }

    void ScreenSpaceReSTIR::prepareLighting(RenderContext* pRenderContext)
    {
        if (is_set(mpScene->getUpdates(), Scene::UpdateFlags::RenderSettingsChanged)) mRecompile = true;

        // Setup alias table for env light.
        if (mpScene->useEnvLight())
        {
            const auto& envMap = mpScene->getEnvMap();
            if (!mpEnvLightLuminance || !mpEnvLightAliasTable)
            {
                const auto& texture = mpScene->getEnvMap()->getEnvMap();
                auto luminances = computeEnvLightLuminance(pRenderContext, texture);
                mpEnvLightLuminance = Buffer::createTyped<float>((uint32_t)luminances.size(), ResourceBindFlags::ShaderResource, Buffer::CpuAccess::None, luminances.data());
                mpEnvLightAliasTable = buildEnvLightAliasTable(texture->getWidth(), texture->getHeight(), luminances, mRng);
                mRecompile = true;
            }

            mEnvLightLuminanceFactor = luminance(envMap->getIntensity() * envMap->getTint());
        }
        else
        {
            if (mpEnvLightLuminance)
            {
                mpEnvLightLuminance = nullptr;
                mpEnvLightAliasTable = nullptr;
                mRecompile = true;
            }
        }

        // Setup alias table for emissive lights.
        if (mpScene->getRenderSettings().useEmissiveLights)
        {
            if (!mpEmissiveLightAliasTable)
            {
                auto lightCollection = mpScene->getLightCollection(pRenderContext);
                lightCollection->update(pRenderContext);
                if (lightCollection->getActiveLightCount() > 0)
                {
                    mpEmissiveTriangles = Buffer::createStructured(mpReflectTypes["emissiveTriangles"], lightCollection->getTotalLightCount(), Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
                    mpEmissiveLightAliasTable = buildEmissiveLightAliasTable(pRenderContext, lightCollection, mRng);
                    mRecompile = true;
                }
            }
        }
        else
        {
            if (mpEmissiveTriangles)
            {
                mpEmissiveTriangles = nullptr;
                mpEmissiveLightAliasTable = nullptr;
                mRecompile = true;
            }
        }

        // Setup alias table for analytic lights.
        if (mpScene->useAnalyticLights())
        {
            if (is_set(mpScene->getUpdates(), Scene::UpdateFlags::LightCountChanged)) mpAnalyticLightAliasTable = nullptr;
            if (!mpAnalyticLightAliasTable)
            {
                std::vector<Light::SharedPtr> lights;
                for (uint32_t i = 0; i < mpScene->getLightCount(); ++i)
                {
                    const auto& light = mpScene->getLight(i);
                    if (light->isActive()) lights.push_back(mpScene->getLight(i));
                }
                if (!lights.empty())
                {
                    mpAnalyticLightAliasTable = buildAnalyticLightAliasTable(pRenderContext, lights, mRng);
                    mRecompile = true;
                }
            }
        }
        else
        {
            if (mpAnalyticLightAliasTable)
            {
                mpAnalyticLightAliasTable = nullptr;
                mRecompile = true;
            }
        }

        // Compute light selection probabilities.
        auto& probs = mLightSelectionProbabilities;
        probs.envLight = mpEnvLightAliasTable ? mOptions->envLightWeight : 0.f;
        probs.emissiveLights = mpEmissiveLightAliasTable ? mOptions->emissiveLightWeight : 0.f;
        probs.analyticLights = mpAnalyticLightAliasTable ? mOptions->analyticLightWeight : 0.f;
        float total = probs.envLight + probs.emissiveLights + probs.analyticLights;
        if (total > 0.f)
        {
            probs.envLight /= total;
            probs.emissiveLights /= total;
            probs.analyticLights /= total;
        }
    }

    void ScreenSpaceReSTIR::updatePrograms()
    {
        if (!mRecompile) return;

        Program::DefineList commonDefines;

        commonDefines.add(getDefines());
        commonDefines.add(mpScene->getSceneDefines());
        commonDefines.add(getLightsDefines());
        commonDefines.add("USE_ALPHA_TEST", mOptions->useAlphaTest ? "1" : "0");
        commonDefines.add("DEBUG_OUTPUT", std::to_string((uint32_t)mOptions->debugOutput));

        // UpdateEmissiveTriangles

        {
            Program::DefineList defines = commonDefines;

            if (!mpUpdateEmissiveTriangles)
            {
                Program::Desc desc;
                desc.addShaderLibrary(kUpdateEmissiveTriangles).csEntry("main").setShaderModel(kShaderModel);
                mpUpdateEmissiveTriangles = ComputePass::create(desc, defines, false);
            }

            mpUpdateEmissiveTriangles->getProgram()->addDefines(defines);
            mpUpdateEmissiveTriangles->setVars(nullptr);
        }

        // GenerateLightTiles

        {
            Program::DefineList defines = commonDefines;

            defines.add("LIGHT_TILE_COUNT", std::to_string(mOptions->lightTileCount));
            defines.add("LIGHT_TILE_SIZE", std::to_string(mOptions->lightTileSize));

            auto [envLightSampleCount, emissiveLightSampleCount, analyticLightSampleCount] = mLightSelectionProbabilities.getSampleCount(mOptions->lightTileSize);
            defines.add("ENV_LIGHT_SAMPLE_COUNT", std::to_string(envLightSampleCount));
            defines.add("EMISSIVE_LIGHT_SAMPLE_COUNT", std::to_string(emissiveLightSampleCount));
            defines.add("ANALYTIC_LIGHT_SAMPLE_COUNT", std::to_string(analyticLightSampleCount));

            if (!mpGenerateLightTiles)
            {
                Program::Desc desc;
                desc.addShaderLibrary(kGenerateLightTilesFile).csEntry("main").setShaderModel(kShaderModel);
                mpGenerateLightTiles = ComputePass::create(desc, defines, false);
            }

            mpGenerateLightTiles->getProgram()->addDefines(defines);
            mpGenerateLightTiles->setVars(nullptr);
        }

        // InitialResampling

        {
            Program::DefineList defines = commonDefines;

            defines.add("LIGHT_TILE_COUNT", std::to_string(mOptions->lightTileCount));
            defines.add("LIGHT_TILE_SIZE", std::to_string(mOptions->lightTileSize));

            defines.add("SCREEN_TILE_SIZE", std::to_string(mOptions->screenTileSize));
            defines.add("INITIAL_LIGHT_SAMPLE_COUNT", std::to_string(mOptions->initialLightSampleCount));
            defines.add("INITIAL_BRDF_SAMPLE_COUNT", std::to_string(mOptions->initialBRDFSampleCount));

            // Only need to check visibility if either temporal or spatial reuse is active.
            bool checkVisibility = mOptions->useInitialVisibility & (mOptions->useTemporalResampling || mOptions->useSpatialResampling);
            defines.add("CHECK_VISIBILITY", checkVisibility ? "1" : "0");

            if (!mpInitialResampling)
            {
                Program::Desc desc;
                desc.addShaderLibrary(kInitialResamplingFile).csEntry("main").setShaderModel(kShaderModel);
                mpInitialResampling = ComputePass::create(desc, defines, false);
            }

            mpInitialResampling->getProgram()->addDefines(defines);
            mpInitialResampling->setVars(nullptr);
        }

        // TemporalResampling

        {
            Program::DefineList defines = commonDefines;

            defines.add("MAX_HISTORY_LENGTH", std::to_string(mOptions->maxHistoryLength));
            // TODO: We currently disable pairwise MIS in the temporal resampling pass.
            // It seems to lead to a lot of variance under camera movement.
            defines.add("USE_PAIRWISE_MIS", "0");
            // TODO: We currently skip shadow rays in the temporal resampling pass.
            // This is not always correct, need to figure out when it needs to be enabled.
            // defines.add("UNBIASED", mOptions->unbiased ? "1" : "0");
            defines.add("UNBIASED", "0");

            if (!mpTemporalResampling)
            {
                Program::Desc desc;
                desc.addShaderLibrary(kTemporalResamplingFile).csEntry("main").setShaderModel(kShaderModel);
                mpTemporalResampling = ComputePass::create(desc, defines, false);
            }

            mpTemporalResampling->getProgram()->addDefines(defines);
            mpTemporalResampling->setVars(nullptr);
        }

        // SpatialResampling

        {
            Program::DefineList defines = commonDefines;

            defines.add("NEIGHBOR_OFFSET_COUNT", std::to_string(mpNeighborOffsets->getWidth()));
            defines.add("USE_PAIRWISE_MIS", mOptions->usePairwiseMIS ? "1" : "0");

            defines.add("UNBIASED", mOptions->unbiased ? "1" : "0");

            if (!mpSpatialResampling)
            {
                Program::Desc desc;
                desc.addShaderLibrary(kSpatialResamplingFile).csEntry("main").setShaderModel(kShaderModel);
                mpSpatialResampling = ComputePass::create(desc, defines, false);
            }

            mpSpatialResampling->getProgram()->addDefines(defines);
            mpSpatialResampling->setVars(nullptr);
        }

        // EvaluateFinalSamples

        {
            Program::DefineList defines = commonDefines;

            defines.add("USE_VISIBILITY", mOptions->useFinalVisibility ? "1" : "0");
            defines.add("REUSE_VISIBILITY", (mOptions->useFinalVisibility && mOptions->reuseFinalVisibility) ? "1" : "0");

            if (!mpEvaluateFinalSamples)
            {
                Program::Desc desc;
                desc.addShaderLibrary(kEvaluateFinalSamplesFile).csEntry("main").setShaderModel(kShaderModel);
                mpEvaluateFinalSamples = ComputePass::create(desc, defines, false);
            }

            mpEvaluateFinalSamples->getProgram()->addDefines(defines);
            mpEvaluateFinalSamples->setVars(nullptr);
        }

        {
            Program::DefineList defines = commonDefines;

            defines.add("RESTIR_MODE", std::to_string((int)mOptions->reSTIRMode));
            defines.add("RESTIR_TARGET_FUNCTION", std::to_string((int)mOptions->targetPdf));
            defines.add("NEIGHBOR_OFFSET_COUNT", std::to_string(mpNeighborOffsets->getWidth()));

            if (!mpGIClearReservoirs)
            {
                Program::Desc desc;
                desc.addShaderLibrary(kGIClearReservoirsFile).csEntry("main").setShaderModel(kShaderModel);
                mpGIClearReservoirs = ComputePass::create(desc, defines, false);
            }
            mpGIClearReservoirs->getProgram()->addDefines(defines);
            mpGIClearReservoirs->setVars(nullptr);

            if (!mpGIResampling)
            {
                Program::Desc desc;
                desc.addShaderLibrary(kGIResamplingFile).csEntry("main").setShaderModel(kShaderModel);
                mpGIResampling = ComputePass::create(desc, defines, false);
            }

            mpGIResampling->getProgram()->addDefines(defines);
            mpGIResampling->setVars(nullptr);
        }

        mRecompile = false;
        mResetTemporalReservoirs = true;
    }

    void ScreenSpaceReSTIR::updateEmissiveTriangles(RenderContext* pRenderContext)
    {
        PROFILE("updateEmissiveTriangles");

        if (!mOptions->useLocalEmissiveTriangles || !mpEmissiveTriangles) return;

        mpUpdateEmissiveTriangles["gScene"] = mpScene->getParameterBlock();

        auto var = mpUpdateEmissiveTriangles["CB"]["gUpdateEmissiveTriangles"];

        uint32_t triangleCount = mpEmissiveTriangles->getElementCount();

        var["emissiveTriangles"] = mpEmissiveTriangles;
        var["emissiveTriangleCount"] = triangleCount;

        mpUpdateEmissiveTriangles->execute(pRenderContext, uint3(triangleCount, 1, 1));
    }

    void ScreenSpaceReSTIR::generateLightTiles(RenderContext* pRenderContext)
    {
        PROFILE("generateLightTiles");

        mpGenerateLightTiles["gScene"] = mpScene->getParameterBlock();

        auto var = mpGenerateLightTiles["CB"]["gGenerateLightTiles"];

        var["lightTileData"] = mpLightTileData;
        setLightsShaderData(var["lights"]);
        var["frameIndex"] = mTotalRISPasses * mFrameIndex + mCurRISPass;
        mCurRISPass += 2;

        mpGenerateLightTiles->execute(pRenderContext, uint3(mOptions->lightTileSize, mOptions->lightTileCount, 1));
    }

    void ScreenSpaceReSTIR::initialResampling(RenderContext* pRenderContext)
    {
        PROFILE("initialResampling");

        mpInitialResampling["gScene"] = mpScene->getParameterBlock();

        auto rootVar = mpInitialResampling->getRootVar();

        mpScene->setRaytracingShaderData(pRenderContext, rootVar);
        mpPixelDebug->prepareProgram(mpInitialResampling->getProgram(), rootVar);

        auto var = rootVar["CB"]["gInitialResampling"];
        var["surfaceData"] = mpSurfaceData;
        var["normalDepth"] = mpNormalDepthTexture;
        var["lightTileData"] = mpLightTileData;
        var["reservoirs"] = mpReservoirs;
        var["debugOutput"] = mpDebugOutputTexture;
        setLightsShaderData(var["lights"]);
        var["frameDim"] = mFrameDim;
        var["frameIndex"] = mTotalRISPasses * mFrameIndex + mCurRISPass;
        var["brdfCutoff"] = mOptions->brdfCutoff;
        mCurRISPass++;

        mpInitialResampling->execute(pRenderContext, mFrameDim.x, mFrameDim.y, 1);
    }

    void ScreenSpaceReSTIR::temporalResampling(RenderContext* pRenderContext, const Texture::SharedPtr& pMotionVectors)
    {
        PROFILE("temporalResampling");

        if (mResetTemporalReservoirs)
        {
            mResetTemporalReservoirs = false;
            return;
        }

        if (!mOptions->useTemporalResampling) return;

        mpTemporalResampling["gScene"] = mpScene->getParameterBlock();

        auto rootVar = mpTemporalResampling->getRootVar();

        mpScene->setRaytracingShaderData(pRenderContext, rootVar);
        mpPixelDebug->prepareProgram(mpTemporalResampling->getProgram(), rootVar);

        auto var = rootVar["CB"]["gTemporalResampling"];
        var["surfaceData"] = mpSurfaceData;
        var["prevSurfaceData"] = mpPrevSurfaceData;
        var["motionVectors"] = pMotionVectors;
        var["reservoirs"] = mpReservoirs;
        var["prevReservoirs"] = mpPrevReservoirs;
        var["debugOutput"] = mpDebugOutputTexture;
        setLightsShaderData(var["lights"]);
        var["frameDim"] = mFrameDim;
        var["frameIndex"] = mTotalRISPasses * mFrameIndex + mCurRISPass;
        var["normalThreshold"] = mOptions->normalThreshold;
        var["depthThreshold"] = mOptions->depthThreshold;
        mCurRISPass++;

        mpTemporalResampling->execute(pRenderContext, mFrameDim.x, mFrameDim.y, 1);
    }

    void ScreenSpaceReSTIR::spatialResampling(RenderContext* pRenderContext)
    {
        PROFILE("spatialResampling");

        if (!mOptions->useSpatialResampling) return;

        mpSpatialResampling["gScene"] = mpScene->getParameterBlock();

        auto rootVar = mpSpatialResampling->getRootVar();

        mpScene->setRaytracingShaderData(pRenderContext, rootVar);
        mpPixelDebug->prepareProgram(mpSpatialResampling->getProgram(), rootVar);

        auto var = rootVar["CB"]["gSpatialResampling"];
        var["surfaceData"] = mpSurfaceData;
        var["normalDepth"] = mpNormalDepthTexture;
        var["debugOutput"] = mpDebugOutputTexture;
        var["neighborOffsets"] = mpNeighborOffsets;
        setLightsShaderData(var["lights"]);
        var["frameDim"] = mFrameDim;
        var["normalThreshold"] = mOptions->normalThreshold;
        var["depthThreshold"] = mOptions->depthThreshold;
        var["neighborCount"] = mOptions->spatialNeighborCount;
        var["gatherRadius"] = (float)mOptions->spatialGatherRadius;

        for (uint32_t iteration = 0; iteration < mOptions->spatialIterations; ++iteration)
        {
            std::swap(mpReservoirs, mpPrevReservoirs);
            var["reservoirs"] = mpReservoirs;
            var["prevReservoirs"] = mpPrevReservoirs;
            var["frameIndex"] = mTotalRISPasses * mFrameIndex + mCurRISPass;
            mCurRISPass += 1;
            mpSpatialResampling->execute(pRenderContext, mFrameDim.x, mFrameDim.y, 1);
        }
    }

    void ScreenSpaceReSTIR::evaluateFinalSamples(RenderContext* pRenderContext)
    {
        PROFILE("evaluateFinalSamples");

        mpEvaluateFinalSamples["gScene"] = mpScene->getParameterBlock();

        auto rootVar = mpEvaluateFinalSamples->getRootVar();

        mpScene->setRaytracingShaderData(pRenderContext, rootVar);
        mpPixelDebug->prepareProgram(mpEvaluateFinalSamples->getProgram(), rootVar);

        auto var = rootVar["CB"]["gEvaluateFinalSamples"];
        var["surfaceData"] = mpSurfaceData;
        var["reservoirs"] = mpReservoirs;
        var["finalSamples"] = mpFinalSamples;
        var["debugOutput"] = mpDebugOutputTexture;
        setLightsShaderData(var["lights"]);
        var["frameDim"] = mFrameDim;

        mpEvaluateFinalSamples->execute(pRenderContext, mFrameDim.x, mFrameDim.y, 1);
    }


    void ScreenSpaceReSTIR::reSTIRGIClearPass(RenderContext* pRenderContext)
    {
        PROFILE("reSTIRGIClearPass");

        // Bind resources.
        auto var = mpGIClearReservoirs->getRootVar()["CB"]["gGIClearReservoirs"];

        var["frameDim"] = mFrameDim;
        var["frameCount"] = mFrameIndex;
        var["forceClearReservoirs"] = mOptions->forceClearReservoirs;
        var["reservoirCount"] = mOptions->reSTIRGIReservoirCount;

        var["initialSamples"] = mpGIInitialSamples;
        var["reservoirBuffer0"] = mpGIReservoirs[0];
        var["reservoirBuffer1"] = mpGIReservoirs[1];

        mpGIClearReservoirs->execute(pRenderContext, mFrameDim.x, mFrameDim.y, 1u);
    }

    Program::DefineList ScreenSpaceReSTIR::getLightsDefines() const
    {
        Program::DefineList defines;

        uint32_t envIndexBits = 26;
        uint32_t envPositionBits = 4;

        uint32_t emissiveIndexBits = 22;
        uint32_t emissivePositionBits = 8;

        uint32_t analyticIndexBits = 14;
        uint32_t analyticPositionBits = 16;

        auto computeIndexPositionBits = [] (const AliasTable::SharedPtr& aliasTable, uint32_t& indexBits, uint32_t& positionBits)
        {
            if (!aliasTable) return;
            uint32_t count = aliasTable->getCount();
            indexBits = 0;
            while (count > 0) {
                ++indexBits;
                count >>= 1;
            }
            if (indexBits & 1) ++indexBits;
            if (indexBits >= 30) throw std::exception("Count too large to be represented in 30 bits");
            positionBits = 30 - indexBits;
        };

        computeIndexPositionBits(mpEnvLightAliasTable, envIndexBits, envPositionBits);
        computeIndexPositionBits(mpEmissiveLightAliasTable, emissiveIndexBits, emissivePositionBits);
        computeIndexPositionBits(mpAnalyticLightAliasTable, analyticIndexBits, analyticPositionBits);

        defines.add("USE_ENV_LIGHT", mpScene->useEnvLight() ? "1" : "0");
        defines.add("USE_EMISSIVE_LIGHTS", mpScene->useEmissiveLights() ? "1" : "0");
        defines.add("USE_ANALYTIC_LIGHTS", mpScene->useAnalyticLights() ? "1" : "0");

        defines.add("LIGHT_SAMPLE_ENV_INDEX_BITS", std::to_string(envIndexBits));
        defines.add("LIGHT_SAMPLE_ENV_POSITION_BITS", std::to_string(envPositionBits));
        defines.add("LIGHT_SAMPLE_EMISSIVE_INDEX_BITS", std::to_string(emissiveIndexBits));
        defines.add("LIGHT_SAMPLE_EMISSIVE_POSITION_BITS", std::to_string(emissivePositionBits));
        defines.add("LIGHT_SAMPLE_ANALYTIC_INDEX_BITS", std::to_string(analyticIndexBits));
        defines.add("LIGHT_SAMPLE_ANALYTIC_POSITION_BITS", std::to_string(analyticPositionBits));

        defines.add("USE_EMISSIVE_TEXTURE_FOR_SAMPLING", mOptions->useEmissiveTextureForSampling ? "1" : "0");
        defines.add("USE_EMISSIVE_TEXTURE_FOR_SHADING", mOptions->useEmissiveTextureForShading ? "1" : "0");
        defines.add("USE_LOCAL_EMISSIVE_TRIANGLES", mOptions->useLocalEmissiveTriangles ? "1" : "0");

        return defines;
    }

    void ScreenSpaceReSTIR::setLightsShaderData(const ShaderVar& var) const
    {
        var["envLightLuminance"] = mpEnvLightLuminance;
        var["emissiveTriangles"] = mpEmissiveTriangles;

        if (mpEnvLightAliasTable) mpEnvLightAliasTable->setShaderData(var["envLightAliasTable"]);
        if (mpEmissiveLightAliasTable) mpEmissiveLightAliasTable->setShaderData(var["emissiveLightAliasTable"]);
        if (mpAnalyticLightAliasTable) mpAnalyticLightAliasTable->setShaderData(var["analyticLightAliasTable"]);

        var["envLightLuminanceFactor"] = mEnvLightLuminanceFactor;

        var["envLightSelectionProbability"] = mLightSelectionProbabilities.envLight;
        var["emissiveLightSelectionProbability"] = mLightSelectionProbabilities.emissiveLights;
        var["analyticLightSelectionProbability"] = mLightSelectionProbabilities.analyticLights;
    }

    std::vector<float> ScreenSpaceReSTIR::computeEnvLightLuminance(RenderContext* pRenderContext, const Texture::SharedPtr& texture)
    {
        assert(texture);

        uint32_t width = texture->getWidth();
        uint32_t height = texture->getHeight();

        // Read texel data from the env map texture so we can create an alias table of samples proportional to intensity.
        std::vector<uint8_t> texelsRaw;
        if (getFormatType(texture->getFormat()) == FormatType::Float)
        {
            texelsRaw = pRenderContext->readTextureSubresource(texture.get(), 0);
        }
        else
        {
            auto floatTexture = Texture::create2D(width, height, ResourceFormat::RGBA32Float, 1, 1, nullptr, ResourceBindFlags::RenderTarget | ResourceBindFlags::ShaderResource);
            pRenderContext->blit(texture->getSRV(), floatTexture->getRTV());
            texelsRaw = pRenderContext->readTextureSubresource(floatTexture.get(), 0);
        }

        uint32_t texelCount = width * height;
        uint32_t channelCount = getFormatChannelCount(texture->getFormat());
        const float* texels = reinterpret_cast<const float*>(texelsRaw.data());

        std::vector<float> luminances(texelCount);

        if (channelCount == 1)
        {
            for (uint32_t i = 0; i < texelCount; ++i)
            {
                luminances[i] = texels[0];
                texels += channelCount;
            }
        }
        else if (channelCount == 3 || channelCount == 4)
        {
            for (uint32_t i = 0; i < texelCount; ++i)
            {
                luminances[i] = luminance(float3(texels[0], texels[1], texels[2]));
                texels += channelCount;
            }
        }
        else
        {
            throw std::exception("Invalid number of channels in env map");
        }

        return luminances;
    }

    AliasTable::SharedPtr ScreenSpaceReSTIR::buildEnvLightAliasTable(uint32_t width, uint32_t height, const std::vector<float>& luminances, std::mt19937& rng)
    {
        assert(luminances.size() == width * height);

        std::vector<float> weights(width * height);

        // Computes weights as luminance multiplied by the texel's solid angle.
        for (uint32_t i = 0, y = 0; y < height; ++y)
        {
            float theta = (float)M_PI * (y + 0.5f) / height;
            float solidAngle = (2.f * (float)M_PI / width) * ((float)M_PI / height) * std::sin(theta);

            for (uint32_t x = 0; x < width; ++x, ++i)
            {
                weights[i] = luminances[i] * solidAngle;
            }
        }

        return AliasTable::create(std::move(weights), rng);
    }

    AliasTable::SharedPtr ScreenSpaceReSTIR::buildEmissiveLightAliasTable(RenderContext* pRenderContext, const LightCollection::SharedPtr& lightCollection, std::mt19937& rng)
    {
        assert(lightCollection);

        lightCollection->update(pRenderContext);

        const auto& triangles = lightCollection->getMeshLightTriangles();

        std::vector<float> weights(triangles.size());

        for (size_t i = 0; i < weights.size(); ++i)
        {
            weights[i] = luminance(triangles[i].averageRadiance) * triangles[i].area;
        }

        return AliasTable::create(std::move(weights), rng);
    }

    AliasTable::SharedPtr ScreenSpaceReSTIR::buildAnalyticLightAliasTable(RenderContext* pRenderContext, const std::vector<Light::SharedPtr>& lights, std::mt19937& rng)
    {
        std::vector<float> weights(lights.size());

        for (size_t i = 0; i < weights.size(); ++i)
        {
            // TODO: Use weight based on light power.
            weights[i] = 1.f;
        }

        return AliasTable::create(std::move(weights), rng);
    }

    Texture::SharedPtr ScreenSpaceReSTIR::createNeighborOffsetTexture(uint32_t sampleCount)
    {
        std::unique_ptr<int8_t[]> offsets(new int8_t[sampleCount * 2]);
        const int R = 254;
        const float phi2 = 1.f / 1.3247179572447f;
        float u = 0.5f;
        float v = 0.5f;
        for (uint32_t index = 0; index < sampleCount * 2;)
        {
            u += phi2;
            v += phi2 * phi2;
            if (u >= 1.f) u -= 1.f;
            if (v >= 1.f) v -= 1.f;

            float rSq = (u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f);
            if (rSq > 0.25f) continue;

            offsets[index++] = int8_t((u - 0.5f) * R);
            offsets[index++] = int8_t((v - 0.5f) * R);
        }

        return Texture::create1D(sampleCount, ResourceFormat::RG8Snorm, 1, 1, offsets.get());
    }

    void ScreenSpaceReSTIR::scriptBindings(pybind11::module& m)
    {
        ScriptBindings::SerializableStruct<Options> options(m, "ScreenSpaceReSTIROptions");
#define field(f_) field(#f_, &Options::f_)
        options.field(useReSTIRDI);
        options.field(useReSTIRGI);
        options.field(normalThreshold);
        options.field(depthThreshold);

        options.field(envLightWeight);
        options.field(emissiveLightWeight);
        options.field(analyticLightWeight);

        options.field(useEmissiveTextureForSampling);
        options.field(useEmissiveTextureForShading);
        options.field(useLocalEmissiveTriangles);

        options.field(lightTileCount);
        options.field(lightTileSize);

        options.field(useAlphaTest);
        options.field(useInitialVisibility);
        options.field(useFinalVisibility);
        options.field(reuseFinalVisibility);

        options.field(screenTileSize);
        options.field(initialLightSampleCount);
        options.field(initialBRDFSampleCount);
        options.field(brdfCutoff);

        options.field(useTemporalResampling);
        options.field(maxHistoryLength);

        options.field(useSpatialResampling);
        options.field(spatialIterations);
        options.field(spatialNeighborCount);
        options.field(spatialGatherRadius);

        options.field(usePairwiseMIS);
        options.field(unbiased);

        options.field(reSTIRGITemporalMaxSamples);
        options.field(reSTIRGISpatialMaxSamples);
        options.field(reSTIRGIReservoirCount);
        options.field(reSTIRGIUseReSTIRN);
        options.field(reSTIRGIMaxSampleAge);
        options.field(diffuseThreshold);
        options.field(reSTIRGIEnableSpatialWeightClamping);
        options.field(forceClearReservoirs);
#undef field
    }
}
