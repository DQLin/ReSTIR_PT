/***************************************************************************
 # Copyright (c) 2022, Daqi Lin.  All rights reserved.
 **************************************************************************/
#pragma once
#include "Falcor.h"
#include "Utils/Debug/PixelDebug.h"
#include "Utils/Sampling/SampleGenerator.h"
#include "Rendering/Lights/EnvMapSampler.h"
#include "Rendering/Lights/EnvMapLighting.h"
#include "Rendering/Lights/EmissiveLightSampler.h"
#include "Rendering/Lights/EmissiveUniformSampler.h"
#include "Rendering/Lights/EmissivePowerSampler.h"
#include "Rendering/Lights/LightBVHSampler.h"
#include "Rendering/Volumes/GridVolumeSampler.h"
#include "Rendering/Utils/PixelStats.h"
#include "Rendering/Materials/TexLODTypes.slang"
#include "Params.slang"
#include <fstream>

using namespace Falcor;

/** Path tracer that uses TraceRayInline() in DXR 1.1.
*/
class ReSTIRPTPass : public RenderPass
{
public:
    using SharedPtr = std::shared_ptr<ReSTIRPTPass>;

    static SharedPtr create(RenderContext* pRenderContext, const Dictionary& dict);

    virtual std::string getDesc() override;
    virtual Dictionary getScriptingDictionary() override;
    Dictionary getSpecializedScriptingDictionary();
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void compile(RenderContext* pContext, const CompileData& compileData) override;
    virtual void setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene) override;
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderUI(Gui::Widgets& widget) override;
    virtual bool onMouseEvent(const MouseEvent& mouseEvent) override;
    virtual bool onKeyEvent(const KeyboardEvent& keyEvent) override { return false; }

    const PixelStats::SharedPtr& getPixelStats() const { return mpPixelStats; }

    void updateDict(const Dictionary& dict) override;
    void initDict() override;

    static void registerBindings(pybind11::module& m);

private:
    ReSTIRPTPass(const Dictionary& dict);
    bool parseDictionary(const Dictionary& dict);
    void validateOptions();
    void updatePrograms();
    void prepareResources(RenderContext* pRenderContext, const RenderData& renderData);
    void setNRDData(const ShaderVar& var, const RenderData& renderData) const;
    void preparePathTracer(const RenderData& renderData);
    void resetLighting();
    void prepareMaterials(RenderContext* pRenderContext);
    bool prepareLighting(RenderContext* pRenderContext);
    void setShaderData(const ShaderVar& var, const RenderData& renderData, bool isPathTracer, bool isPathGenerator) const;
    bool renderRenderingUI(Gui::Widgets& widget);
    bool renderDebugUI(Gui::Widgets& widget);
    bool renderStatsUI(Gui::Widgets& widget);
    bool beginFrame(RenderContext* pRenderContext, const RenderData& renderData);
    void endFrame(RenderContext* pRenderContext, const RenderData& renderData);
    void generatePaths(RenderContext* pRenderContext, const RenderData& renderData, int sampleId = 0);
    void tracePass(RenderContext* pRenderContext, const RenderData& renderData, const ComputePass::SharedPtr& pass, const std::string& passName, int sampleId);
    void PathReusePass(RenderContext* pRenderContext, uint32_t restir_i, const RenderData& renderData, bool temporalReuse = false, int spatialRoundId = 0, bool isLastRound = false);
    void PathRetracePass(RenderContext* pRenderContext, uint32_t restir_i, const RenderData& renderData, bool temporalReuse = false, int spatialRoundId = 0);
    Texture::SharedPtr createNeighborOffsetTexture(uint32_t sampleCount);

    /** Static configuration. Changing any of these options require shader recompilation.
    */
    struct StaticParams
    {
        // Rendering parameters
        uint32_t    samplesPerPixel = 1;                        ///< Number of samples (paths) per pixel, unless a sample density map is used.
        uint32_t    candidateSamples = 1;
        uint32_t    maxSurfaceBounces = 9;                      ///< Max number of surface bounces (diffuse + specular + transmission), up to kMaxPathLenth.
        uint32_t    maxDiffuseBounces = -1;                     ///< Max number of diffuse bounces (0 = direct only), up to kMaxBounces. This will be initialized at startup.
        uint32_t    maxSpecularBounces = -1;                    ///< Max number of specular bounces (0 = direct only), up to kMaxBounces. This will be initialized at startup.
        uint32_t    maxTransmissionBounces = -1;                ///< Max number of transmission bounces (0 = none), up to kMaxBounces. This will be initialized at startup.
        uint32_t    sampleGenerator = SAMPLE_GENERATOR_TINY_UNIFORM; ///< Pseudorandom sample generator type.
        bool        adjustShadingNormals = false;               ///< Adjust shading normals on secondary hits.
        bool        useBSDFSampling = true;                     ///< Use BRDF importance sampling, otherwise cosine-weighted hemisphere sampling.
        bool        useNEE = true;                              ///< Use next-event estimation (NEE). This enables shadow ray(s) from each path vertex.
        bool        useMIS = true;                              ///< Use multiple importance sampling (MIS) when NEE is enabled.
        bool        useRussianRoulette = false;                 ///< Use russian roulette to terminate low throughput paths.
        bool        useAlphaTest = true;                        ///< Use alpha testing on non-opaque triangles.
        uint32_t    maxNestedMaterials = 2;                     ///< Maximum supported number of nested materials.
        bool        useLightsInDielectricVolumes = false;       ///< Use lights inside of volumes (transmissive materials). We typically don't want this because lights are occluded by the interface.
        bool        limitTransmission = false;                  ///< Limit specular transmission by handling reflection/refraction events only up to a given transmission depth.
        uint32_t    maxTransmissionReflectionDepth = 0;         ///< Maximum transmission depth at which to sample specular reflection.
        uint32_t    maxTransmissionRefractionDepth = 0;         ///< Maximum transmission depth at which to sample specular refraction (after that, IoR is set to 1).
        bool        disableCaustics = false;                    ///< Disable sampling of caustics.
        bool        disableDirectIllumination = true;          ///< Disable all direct illumination.
        TexLODMode  primaryLodMode = TexLODMode::Mip0;          ///< Use filtered texture lookups at the primary hit.
        ColorFormat colorFormat = ColorFormat::LogLuvHDR;       ///< Color format used for internal per-sample color and denoiser buffers.
        MISHeuristic misHeuristic = MISHeuristic::Balance;      ///< MIS heuristic.
        float       misPowerExponent = 2.f;                     ///< MIS exponent for the power heuristic. This is only used when 'PowerExp' is chosen.
        EmissiveLightSamplerType emissiveSampler = EmissiveLightSamplerType::Power;  ///< Emissive light sampler to use for NEE.

        bool        useDeterministicBSDF = true;                    ///< Evaluate all compatible lobes at BSDF sampling time.

        ReSTIRMISKind    spatialMisKind = ReSTIRMISKind::Pairwise;
        ReSTIRMISKind    temporalMisKind = ReSTIRMISKind::Talbot;

        ShiftMapping    shiftStrategy = ShiftMapping::Hybrid;
        bool            temporalUpdateForDynamicScene = false;

        PathSamplingMode pathSamplingMode = PathSamplingMode::ReSTIR;

        bool            separatePathBSDF = true;

        bool            rcDataOfflineMode = false;

		// Denoising parameters
		bool        useNRDDemodulation = true;                  ///< Global switch for NRD demodulation.

        Program::DefineList getDefines(const ReSTIRPTPass& owner) const;
    };


    void Init()
    {
        mStaticParams = StaticParams();
        mParams = RestirPathTracerParams();
        mEnableTemporalReuse = true;
        mEnableSpatialReuse = true;
        mSpatialReusePattern = SpatialReusePattern::Default;
        mPathReusePattern = PathReusePattern::NRooksShift;
        mSmallWindowRestirWindowRadius = 2;
        mSpatialNeighborCount = 3;
        mSpatialReuseRadius = 20.f;
        mNumSpatialRounds = 1;
        mEnableTemporalReprojection = false;
        mUseMaxHistory = true;
        mUseDirectLighting = true;
        mTemporalHistoryLength = 20;
        mNoResamplingForTemporalReuse = false;
    }

    // Configuration
    RestirPathTracerParams          mParams;                    ///< Runtime path tracer parameters.
    StaticParams                    mStaticParams;              ///< Static parameters. These are set as compile-time constants in the shaders.
    LightBVHSampler::Options        mLightBVHOptions;           ///< Current options for the light BVH sampler.

    // Internal state
    Scene::SharedPtr                mpScene;                    ///< The current scene, or nullptr if no scene loaded.
    SampleGenerator::SharedPtr      mpSampleGenerator;          ///< GPU pseudo-random sample generator.
    EnvMapSampler::SharedPtr        mpEnvMapSampler;            ///< Environment map sampler or nullptr if not used.
    EmissiveLightSampler::SharedPtr mpEmissiveSampler;          ///< Emissive light sampler or nullptr if not used.
    PixelStats::SharedPtr           mpPixelStats;               ///< Utility class for collecting pixel stats.
    PixelDebug::SharedPtr           mpPixelDebug;               ///< Utility class for pixel debugging (print in shaders).
    ParameterBlock::SharedPtr       mpPathTracerBlock;          ///< Parameter block for the path tracer.

    // internal below
    bool                            mRecompile = false;         ///< Set to true when program specialization has changed.
    bool                            mVarsChanged = true;        ///< This is set to true whenever the program vars have changed and resources need to be rebound.
    bool                            mOptionsChanged = false;    ///< True if the config has changed since last frame.
    bool                            mGBufferAdjustShadingNormals = false; ///< True if GBuffer/VBuffer has adjusted shading normals enabled.
    bool                            mOutputTime = false;        ///< True if time data should be generated as output.
    bool                            mOutputNRDData = false;     ///< True if NRD diffuse/specular data should be generated as outputs.
    bool                            mEnableRayStats = false;      ///< Set to true when the stats tab in the UI is open. This may negatively affect performance.

    uint64_t                        mAccumulatedRayCount = 0;
    uint64_t                        mAccumulatedClosestHitRayCount = 0;
    uint64_t                        mAccumulatedShadowRayCount = 0;

    // params below
    bool                            mEnableTemporalReuse = true;
    bool                            mEnableSpatialReuse = true;
    SpatialReusePattern             mSpatialReusePattern = SpatialReusePattern::Default;
    PathReusePattern                mPathReusePattern = PathReusePattern::NRooksShift;
    uint32_t                        mSmallWindowRestirWindowRadius = 2;
    int                             mSpatialNeighborCount = 3;
    float                           mSpatialReuseRadius = 20.f;
    int                             mNumSpatialRounds = 1;

    bool                            mEnableTemporalReprojection = true;
    bool                            mFeatureBasedRejection = true;

    bool                            mUseMaxHistory = true;

    int                             mReservoirFrameCount = 0; // internal

    bool                            mUseDirectLighting = true;

    int                             mTemporalHistoryLength = 20;
    bool                            mNoResamplingForTemporalReuse = false;
    int                             mSeedOffset = 0;


    bool mResetRenderPassFlags = false;

    ComputePass::SharedPtr          mpSpatialReusePass;      ///< Merges reservoirs.
    ComputePass::SharedPtr          mpTemporalReusePass;      ///< Merges reservoirs.
    ComputePass::SharedPtr          mpComputePathReuseMISWeightsPass;

    ComputePass::SharedPtr          mpSpatialPathRetracePass;
    ComputePass::SharedPtr          mpTemporalPathRetracePass;

    ComputePass::SharedPtr          mpGeneratePaths;                ///< Fullscreen compute pass generating paths starting at primary hits.
    ComputePass::SharedPtr          mpTracePass;                    ///< Main tracing pass.

    ComputePass::SharedPtr          mpReflectTypes;             ///< Helper for reflecting structured buffer types.

    GpuFence::SharedPtr             mpReadbackFence;            ///< GPU fence for synchronizing stats readback.

    // Data
    Buffer::SharedPtr               mpCounters;                 ///< Atomic counters (32-bit).
    Buffer::SharedPtr               mpCountersReadback;         ///< Readback buffer for the counters for stats gathering.

    Buffer::SharedPtr               mpOutputReservoirs;               ///< Output paths from the path sampling stage.
    // enable multiple temporal reservoirs for spp > 1 (multiple ReSTIR chains)
    std::vector<Buffer::SharedPtr>               mpTemporalReservoirs;               ///< Output paths from the path sampling stage.
    Buffer::SharedPtr               mReconnectionDataBuffer;
    Buffer::SharedPtr               mPathReuseMISWeightBuffer;

    Texture::SharedPtr              mpTemporalVBuffer;

    Texture::SharedPtr              mpNeighborOffsets;

    Buffer::SharedPtr               mNRooksPatternBuffer;
};
