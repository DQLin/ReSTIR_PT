/***************************************************************************
 # Copyright (c) 2022, Daqi Lin.  All rights reserved.
 **************************************************************************/
#include "ReSTIRPTPass.h"
#include "RenderGraph/RenderPassHelpers.h"
#include <fstream>

namespace
{
    const char kDesc[] = "Path tracer using DXR 1.1 TraceRayInline";

    const std::string kGeneratePathsFilename = "RenderPasses/ReSTIRPTPass/GeneratePaths.cs.slang";
    const std::string kTracePassFilename = "RenderPasses/ReSTIRPTPass/TracePass.cs.slang";
    const std::string kReflectTypesFile = "RenderPasses/ReSTIRPTPass/ReflectTypes.cs.slang";
    const std::string kSpatialReusePassFile = "RenderPasses/ReSTIRPTPass/SpatialReuse.cs.slang";
    const std::string kTemporalReusePassFile = "RenderPasses/ReSTIRPTPass/TemporalReuse.cs.slang";
    const std::string kSpatialPathRetraceFile = "RenderPasses/ReSTIRPTPass/SpatialPathRetrace.cs.slang";
    const std::string kTemporalPathRetraceFile = "RenderPasses/ReSTIRPTPass/TemporalPathRetrace.cs.slang";
    const std::string kComputePathReuseMISWeightsFile = "RenderPasses/ReSTIRPTPass/ComputePathReuseMISWeights.cs.slang";

    // Render pass inputs and outputs.
    const std::string kInputVBuffer = "vbuffer";
    const std::string kInputMotionVectors = "motionVectors";
    const std::string kInputDirectLighting = "directLighting";

    const Falcor::ChannelList kInputChannels =
    {
        { kInputVBuffer,        "gVBuffer",         "Visibility buffer in packed format", false, ResourceFormat::Unknown },
        { kInputMotionVectors,  "gMotionVectors",   "Motion vector buffer (float format)", true /* optional */, ResourceFormat::RG32Float },
        { kInputDirectLighting,    "gDirectLighting",     "Sample count buffer (integer format)", true /* optional */, ResourceFormat::RGBA32Float },
    };

    const std::string kOutputColor = "color";
    const std::string kOutputAlbedo = "albedo";
    const std::string kOutputSpecularAlbedo = "specularAlbedo";
    const std::string kOutputIndirectAlbedo = "indirectAlbedo";
    const std::string kOutputNormal = "normal";
    const std::string kOutputReflectionPosW = "reflectionPosW";
    const std::string kOutputRayCount = "rayCount";
    const std::string kOutputPathLength = "pathLength";
    const std::string kOutputDebug = "debug";
    const std::string kOutputTime = "time";
    const std::string kOutputNRDDiffuseRadianceHitDist = "nrdDiffuseRadianceHitDist";
    const std::string kOutputNRDSpecularRadianceHitDist = "nrdSpecularRadianceHitDist";
    const std::string kOutputNRDResidualRadianceHitDist = "nrdResidualRadianceHitDist";
    const std::string kOutputNRDEmission = "nrdEmission";
    const std::string kOutputNRDDiffuseReflectance = "nrdDiffuseReflectance";
    const std::string kOutputNRDSpecularReflectance = "nrdSpecularReflectance";


    const Falcor::ChannelList kOutputChannels =
    {
        { kOutputColor,                 "gOutputColor",                 "Output color (linear)", true /* optional */ },
        { kOutputAlbedo,                "gOutputAlbedo",                "Output albedo (linear)", true /* optional */, ResourceFormat::RGBA8Unorm },
        { kOutputNormal,                "gOutputNormal",                "Output normal (linear)", true /* optional */, ResourceFormat::RGBA16Float },
        { kOutputRayCount,              "",                             "Per-pixel ray count", true /* optional */, ResourceFormat::R32Uint },
        { kOutputPathLength,            "",                             "Per-pixel path length", true /* optional */, ResourceFormat::R32Uint },
        { kOutputDebug,                 "",                             "Debug output", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputTime,                  "",                             "Per-pixel time", true /* optional */, ResourceFormat::R32Uint },
        { kOutputSpecularAlbedo,                "gOutputSpecularAlbedo",                "Output specular albedo (linear)", true /* optional */, ResourceFormat::RGBA8Unorm },
        { kOutputIndirectAlbedo,                "gOutputIndirectAlbedo",                "Output indirect albedo (linear)", true /* optional */, ResourceFormat::RGBA8Unorm },
        { kOutputReflectionPosW,                "gOutputReflectionPosW",                "Output reflection pos (world space)", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputNRDDiffuseRadianceHitDist,     "gOutputNRDDiffuseRadianceHitDist",     "Output demodulated diffuse color (linear) and hit distance", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputNRDSpecularRadianceHitDist,    "gOutputNRDSpecularRadianceHitDist",    "Output demodulated specular color (linear) and hit distance", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputNRDResidualRadianceHitDist,    "gOutputNRDResidualRadianceHitDist",    "Output residual color (linear) and hit distance", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputNRDEmission,                   "gOutputNRDEmission",                   "Output primary surface emission", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputNRDDiffuseReflectance,         "gOutputNRDDiffuseReflectance",         "Output primary surface diffuse reflectance", true /* optional */, ResourceFormat::RGBA16Float },
        { kOutputNRDSpecularReflectance,        "gOutputNRDSpecularReflectance",        "Output primary surface specular reflectance", true /* optional */, ResourceFormat::RGBA16Float },
    };

    // UI variables.
    const Gui::DropdownList kColorFormatList =
    {
        { (uint32_t)ColorFormat::RGBA32F, "RGBA32F (128bpp)" },
        { (uint32_t)ColorFormat::LogLuvHDR, "LogLuvHDR (32bpp)" },
    };

    const Gui::DropdownList kMISHeuristicList =
    {
        { (uint32_t)MISHeuristic::Balance, "Balance heuristic" },
        { (uint32_t)MISHeuristic::PowerTwo, "Power heuristic (exp=2)" },
        { (uint32_t)MISHeuristic::PowerExp, "Power heuristic" },
    };

    const Gui::DropdownList kShiftMappingList =
    {
        { (uint32_t)ShiftMapping::Reconnection, "Reconnection" },
        { (uint32_t)ShiftMapping::RandomReplay, "Random Replay" },
        { (uint32_t)ShiftMapping::Hybrid, "Hybrid" },
    };

    const Gui::DropdownList kReSTIRMISList =
    {
        { (uint32_t)ReSTIRMISKind::Constant , "Constant resampling MIS (with balance-heuristic contribution MIS)" },
        { (uint32_t)ReSTIRMISKind::Talbot, "Talbot resampling MIS" },
        { (uint32_t)ReSTIRMISKind::Pairwise, "Pairwise resampling MIS" },
        { (uint32_t)ReSTIRMISKind::ConstantBinary, "Constant resampling MIS (with 1/|Z| contribution MIS)" },
        { (uint32_t)ReSTIRMISKind::ConstantBiased, "Constant resampling MIS (constant contribution MIS, biased)" },
    };

    const Gui::DropdownList kReSTIRMISList2 =
    {
        { (uint32_t)ReSTIRMISKind::Constant , "Constant resampling MIS (with balance-heuristic contribution MIS)" },
        { (uint32_t)ReSTIRMISKind::Talbot, "Talbot resampling MIS" },
        { (uint32_t)ReSTIRMISKind::ConstantBinary, "Constant resampling MIS (with 1/|Z| contribution MIS)" },
        { (uint32_t)ReSTIRMISKind::ConstantBiased, "Constant resampling MIS (constant contribution MIS, biased)" },
    };

    const Gui::DropdownList kPathReusePatternList =
    {
        { (uint32_t)PathReusePattern::Block, std::string("Block")},
        { (uint32_t)PathReusePattern::NRooks, std::string("N-Rooks")},
        { (uint32_t)PathReusePattern::NRooksShift, std::string("N-Rooks Shift")},
    };

    const Gui::DropdownList kSpatialReusePatternList =
    {
        { (uint32_t)SpatialReusePattern::Default, std::string("Default")},
        { (uint32_t)SpatialReusePattern::SmallWindow, std::string("Small Window")},
    };

    const Gui::DropdownList kEmissiveSamplerList =
    {
        { (uint32_t)EmissiveLightSamplerType::Uniform, "Uniform" },
        { (uint32_t)EmissiveLightSamplerType::LightBVH, "LightBVH" },
        { (uint32_t)EmissiveLightSamplerType::Power, "Power" },
    };

    const Gui::DropdownList kLODModeList =
    {
        { (uint32_t)TexLODMode::Mip0, "Mip0" },
        { (uint32_t)TexLODMode::RayDiffs, "Ray Diffs" }
    };

    const Gui::DropdownList kPathSamplingModeList =
    {
        { (uint32_t)PathSamplingMode::ReSTIR, "ReSTIR PT" },
        { (uint32_t)PathSamplingMode::PathReuse, "Bekaert-style Path Reuse" },
        { (uint32_t)PathSamplingMode::PathTracing, "Path Tracing" }
    };

    // Scripting options.
    const std::string kSamplesPerPixel = "samplesPerPixel";
    const std::string kMaxSurfaceBounces = "maxSurfaceBounces";
    const std::string kMaxDiffuseBounces = "maxDiffuseBounces";
    const std::string kMaxSpecularBounces = "maxSpecularBounces";
    const std::string kMaxTransmissionBounces = "maxTransmissionBounces";
    const std::string kAdjustShadingNormals = "adjustShadingNormals";
    const std::string kLODBias = "lodBias";
    const std::string kSampleGenerator = "sampleGenerator";
    const std::string kUseBSDFSampling = "useBSDFSampling";
    const std::string kUseNEE = "useNEE";
    const std::string kUseMIS = "useMIS";
    const std::string kUseRussianRoulette = "useRussianRoulette";
    const std::string kScreenSpaceReSTIROptions = "screenSpaceReSTIROptions";
    const std::string kUseAlphaTest = "useAlphaTest";
    const std::string kMaxNestedMaterials = "maxNestedMaterials";
    const std::string kUseLightsInDielectricVolumes = "useLightsInDielectricVolumes";
    const std::string kLimitTransmission = "limitTransmission";
    const std::string kMaxTransmissionReflectionDepth = "maxTransmissionReflectionDepth";
    const std::string kMaxTransmissionRefractionDepth = "maxTransmissionRefractionDepth";
    const std::string kDisableCaustics = "disableCaustics";
    const std::string kSpecularRoughnessThreshold = "specularRoughnessThreshold";
    const std::string kDisableDirectIllumination = "disableDirectIllumination";
    const std::string kColorFormat = "colorFormat";
    const std::string kMISHeuristic = "misHeuristic";
    const std::string kMISPowerExponent = "misPowerExponent";
    const std::string kFixedSeed = "fixedSeed";
    const std::string kEmissiveSampler = "emissiveSampler";
    const std::string kLightBVHOptions = "lightBVHOptions";
    const std::string kPrimaryLodMode = "primaryLodMode";
    const std::string kUseNRDDemodulation = "useNRDDemodulation";

    const std::string kSpatialMisKind = "spatialMisKind";
    const std::string kTemporalMisKind = "temporalMisKind";
    const std::string kShiftStrategy = "shiftStrategy";
    const std::string kRejectShiftBasedOnJacobian = "rejectShiftBasedOnJacobian";
    const std::string kJacobianRejectionThreshold = "jacobianRejectionThreshold";
    const std::string kNearFieldDistance = "nearFieldDistance";
    const std::string kLocalStrategyType = "localStrategyType";

    const std::string kTemporalHistoryLength = "temporalHistoryLength";
    const std::string kUseMaxHistory = "useMaxHistory";
    const std::string kSeedOffset = "seedOffset";
    const std::string kEnableTemporalReuse = "enableTemporalReuse";
    const std::string kEnableSpatialReuse = "enableSpatialReuse";
    const std::string kNumSpatialRounds = "numSpatialRounds";
    const std::string kPathSamplingMode = "pathSamplingMode";
    const std::string kEnableTemporalReprojection = "enableTemporalReprojection";
    const std::string kNoResamplingForTemporalReuse = "noResamplingForTemporalReuse";
    const std::string kSpatialNeighborCount = "spatialNeighborCount";
    const std::string kFeatureBasedRejection = "featureBasedRejection";
    const std::string kSpatialReusePattern = "spatialReusePattern";
    const std::string kSmallWindowRestirWindowRadius = "smallWindowRestirWindowRadius";
    const std::string kSpatialReuseRadius = "spatialReuseRadius";
    const std::string kUseDirectLighting = "useDirectLighting";
    const std::string kSeparatePathBSDF = "separatePathBSDF";
    const std::string kCandidateSamples = "candidateSamples";
    const std::string kTemporalUpdateForDynamicScene = "temporalUpdateForDynamicScene";
    const std::string kEnableRayStats = "enableRayStats";

    const uint32_t kNeighborOffsetCount = 8192;
}

// Don't remove this. it's required for hot-reload to function properly
extern "C" __declspec(dllexport) const char* getProjDir()
{
    return PROJECT_DIR;
}

extern "C" __declspec(dllexport) void getPasses(Falcor::RenderPassLibrary & lib)
{
    lib.registerClass("ReSTIRPTPass", kDesc, ReSTIRPTPass::create);
    ScriptBindings::registerBinding(ReSTIRPTPass::registerBindings);
}


void ReSTIRPTPass::updateDict(const Dictionary& dict)
{
    // cleanToDefaultValue
    bool needToReset = parseDictionary(dict);
    if (needToReset)
    {
        validateOptions();
        mOptionsChanged = true;
        mRecompile = true;
        mParams.frameCount = 0;
        mAccumulatedShadowRayCount = 0;
        mAccumulatedClosestHitRayCount = 0;
        mAccumulatedRayCount = 0;
    }
}

void ReSTIRPTPass::initDict()
{
    Init();
    mOptionsChanged = true;
    mRecompile = true;
    mParams.frameCount = 0;
}

void ReSTIRPTPass::registerBindings(pybind11::module& m)
{
    //pybind11::enum_<ColorFormat> colorFormat(m, "ColorFormat");
    //colorFormat.value("RGBA32F", ColorFormat::RGBA32F);
    //colorFormat.value("LogLuvHDR", ColorFormat::LogLuvHDR);

    //pybind11::enum_<MISHeuristic> misHeuristic(m, "MISHeuristic");
    //misHeuristic.value("Balance", MISHeuristic::Balance);
    //misHeuristic.value("PowerTwo", MISHeuristic::PowerTwo);
    //misHeuristic.value("PowerExp", MISHeuristic::PowerExp);

    pybind11::enum_<ShiftMapping> shiftMapping(m, "ShiftMapping");
    shiftMapping.value("Reconnection", ShiftMapping::Reconnection);
    shiftMapping.value("RandomReplay", ShiftMapping::RandomReplay);
    shiftMapping.value("Hybrid", ShiftMapping::Hybrid);

    pybind11::enum_<ReSTIRMISKind> misKind(m, "ReSTIRMISKind");
    misKind.value("Constant", ReSTIRMISKind::Constant);
    misKind.value("Talbot", ReSTIRMISKind::Talbot);
    misKind.value("Pairwise", ReSTIRMISKind::Pairwise);
    misKind.value("ConstantBinary", ReSTIRMISKind::ConstantBinary);
    misKind.value("ConstantBiased", ReSTIRMISKind::ConstantBiased);

    pybind11::enum_<PathSamplingMode> pathSamplingMode(m, "PathSamplingMode");
    pathSamplingMode.value("ReSTIR", PathSamplingMode::ReSTIR);
    pathSamplingMode.value("PathReuse", PathSamplingMode::PathReuse);
    pathSamplingMode.value("PathTracing", PathSamplingMode::PathTracing);

    pybind11::enum_<SpatialReusePattern> spatialReusePattern(m, "SpatialReusePattern");
    spatialReusePattern.value("Default", SpatialReusePattern::Default);
    spatialReusePattern.value("SmallWindow", SpatialReusePattern::SmallWindow);

    pybind11::class_<ReSTIRPTPass, RenderPass, ReSTIRPTPass::SharedPtr> pass(m, "ReSTIRPTPass");
    pass.def_property_readonly("pixelStats", &ReSTIRPTPass::getPixelStats);

    pass.def_property("useFixedSeed",
        [](const ReSTIRPTPass* pt) { return pt->mParams.useFixedSeed ? true : false; },
        [](ReSTIRPTPass* pt, bool value) { pt->mParams.useFixedSeed = value ? 1 : 0; }
    );
    pass.def_property("fixedSeed",
        [](const ReSTIRPTPass* pt) { return pt->mParams.fixedSeed; },
        [](ReSTIRPTPass* pt, uint32_t value) { pt->mParams.fixedSeed = value; }
    );
}

std::string ReSTIRPTPass::getDesc() { return kDesc; }

ReSTIRPTPass::SharedPtr ReSTIRPTPass::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    return SharedPtr(new ReSTIRPTPass(dict));
}

ReSTIRPTPass::ReSTIRPTPass(const Dictionary& dict)
{
    if (!gpDevice->isFeatureSupported(Device::SupportedFeatures::RaytracingTier1_1))
    {
        throw std::exception("Raytracing Tier 1.1 is not supported by the current device");
    }

    parseDictionary(dict);
    validateOptions();

    // load N-rook patterns (for Bekaert-style path reuse)

    std::string fullpath;
    findFileInDataDirectories("16RooksPattern256.txt", fullpath);
    FILE* f = fopen(fullpath.c_str(), "r");

    std::vector<byte> NRookArray(65536);
    for (int i = 0; i < 8192; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            int temp1, temp2;
            fscanf(f, "%d %d", &temp1, &temp2);
            NRookArray[8 * i + j] = (temp2 << 4) | temp1;
        }
    }
    fclose(f);

    mNRooksPatternBuffer = Buffer::create(65536, ResourceBindFlags::ShaderResource, Buffer::CpuAccess::None, NRookArray.data());

    // Create sample generator.
    mpSampleGenerator = SampleGenerator::create(mStaticParams.sampleGenerator);

    // Create neighbor offset texture.
    mpNeighborOffsets = createNeighborOffsetTexture(kNeighborOffsetCount);

    // Create programs.
    auto defines = mStaticParams.getDefines(*this);

    mpGeneratePaths = ComputePass::create(kGeneratePathsFilename, "main", defines, false);
    mpReflectTypes = ComputePass::create(kReflectTypesFile, "main", defines, false);

    {
        Program::Desc desc;
        desc.addShaderLibrary(kTracePassFilename).csEntry("main").setShaderModel("6_5");
        mpTracePass = ComputePass::create(desc, defines, false);
    }

    {
        Program::Desc desc;
        desc.addShaderLibrary(kSpatialPathRetraceFile).csEntry("main").setShaderModel("6_5");
        mpSpatialPathRetracePass = ComputePass::create(desc, defines, false);
    }

    {
        Program::Desc desc;
        desc.addShaderLibrary(kTemporalPathRetraceFile).csEntry("main").setShaderModel("6_5");
        mpTemporalPathRetracePass = ComputePass::create(desc, defines, false);
    }

    {
        Program::Desc desc;
        desc.addShaderLibrary(kSpatialReusePassFile).csEntry("main").setShaderModel("6_5");
        mpSpatialReusePass = ComputePass::create(desc, defines, false);
    }

    {
        Program::Desc desc;
        desc.addShaderLibrary(kTemporalReusePassFile).csEntry("main").setShaderModel("6_5");
        mpTemporalReusePass = ComputePass::create(desc, defines, false);
    }

    {
        Program::Desc desc;
        desc.addShaderLibrary(kComputePathReuseMISWeightsFile).csEntry("main").setShaderModel("6_5");
        mpComputePathReuseMISWeightsPass = ComputePass::create(desc, defines, false);
    }

    // Allocate resources that don't change in size.
    mpCounters = Buffer::create((size_t)Counters::kCount * sizeof(uint32_t), Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None);
    mpCountersReadback = Buffer::create((size_t)Counters::kCount * sizeof(uint32_t), Resource::BindFlags::None, Buffer::CpuAccess::Read);

    mpPixelStats = PixelStats::create();
    mpPixelDebug = PixelDebug::create(1000);

    mpReadbackFence = GpuFence::create();
}

bool ReSTIRPTPass::parseDictionary(const Dictionary& dict)
{
    bool needToReset = true;
    for (const auto& [key, value] : dict)
    {
        if (key == kSamplesPerPixel) mStaticParams.samplesPerPixel = value;
        else if (key == kMaxSurfaceBounces) mStaticParams.maxSurfaceBounces = value;
        else if (key == kMaxDiffuseBounces) mStaticParams.maxDiffuseBounces = value;
        else if (key == kMaxSpecularBounces) mStaticParams.maxSpecularBounces = value;
        else if (key == kMaxTransmissionBounces) mStaticParams.maxTransmissionBounces = value;
        else if (key == kAdjustShadingNormals) mStaticParams.adjustShadingNormals = value;
        else if (key == kLODBias) mParams.lodBias = value;
        else if (key == kSampleGenerator) mStaticParams.sampleGenerator = value;
        else if (key == kFixedSeed) { mParams.fixedSeed = value; mParams.useFixedSeed = true; }
        else if (key == kUseBSDFSampling) mStaticParams.useBSDFSampling = value;
        else if (key == kUseNEE) mStaticParams.useNEE = value;
        else if (key == kUseMIS) mStaticParams.useMIS = value;
        else if (key == kUseRussianRoulette) mStaticParams.useRussianRoulette = value;
        else if (key == kUseAlphaTest) mStaticParams.useAlphaTest = value;
        else if (key == kMaxNestedMaterials) mStaticParams.maxNestedMaterials = value;
        else if (key == kUseLightsInDielectricVolumes) mStaticParams.useLightsInDielectricVolumes = value;
        else if (key == kLimitTransmission) mStaticParams.limitTransmission = value;
        else if (key == kMaxTransmissionReflectionDepth) mStaticParams.maxTransmissionReflectionDepth = value;
        else if (key == kMaxTransmissionRefractionDepth) mStaticParams.maxTransmissionRefractionDepth = value;
        else if (key == kDisableCaustics) mStaticParams.disableCaustics = value;
        else if (key == kSpecularRoughnessThreshold) mParams.specularRoughnessThreshold = value;
        else if (key == kDisableDirectIllumination) mStaticParams.disableDirectIllumination = value;
        else if (key == kPrimaryLodMode)  mStaticParams.primaryLodMode = value;
        // Denoising parameters
        else if (key == kUseNRDDemodulation) mStaticParams.useNRDDemodulation = value;
        else if (key == kColorFormat) mStaticParams.colorFormat = value;
        else if (key == kMISHeuristic) mStaticParams.misHeuristic = value;
        else if (key == kMISPowerExponent) mStaticParams.misPowerExponent = value;
        else if (key == kEmissiveSampler) mStaticParams.emissiveSampler = value;
        else if (key == kLightBVHOptions) mLightBVHOptions = value;
        else if (key == kSpatialMisKind) mStaticParams.spatialMisKind = value;
        else if (key == kTemporalMisKind) mStaticParams.temporalMisKind = value;
        else if (key == kShiftStrategy) mStaticParams.shiftStrategy = value;
        else if (key == kRejectShiftBasedOnJacobian) mParams.rejectShiftBasedOnJacobian = value;
        else if (key == kJacobianRejectionThreshold) mParams.jacobianRejectionThreshold = value;
        else if (key == kNearFieldDistance) mParams.nearFieldDistance = value;
        else if (key == kTemporalHistoryLength) mTemporalHistoryLength = value;
        else if (key == kUseMaxHistory) mUseMaxHistory = value;
        else if (key == kSeedOffset) mSeedOffset = value;
        else if (key == kEnableTemporalReuse) mEnableTemporalReuse = value;
        else if (key == kEnableSpatialReuse) mEnableSpatialReuse = value;
        else if (key == kNumSpatialRounds) mNumSpatialRounds = value;
        else if (key == kPathSamplingMode) mStaticParams.pathSamplingMode = value;
        else if (key == kLocalStrategyType) mParams.localStrategyType = value;
        else if (key == kEnableTemporalReprojection) mEnableTemporalReprojection = value;
        else if (key == kNoResamplingForTemporalReuse) mNoResamplingForTemporalReuse = value;
        else if (key == kSpatialNeighborCount) mSpatialNeighborCount = value;
        else if (key == kFeatureBasedRejection) mFeatureBasedRejection = value;
        else if (key == kSpatialReusePattern) mSpatialReusePattern = value;
        else if (key == kSmallWindowRestirWindowRadius) mSmallWindowRestirWindowRadius = value;
        else if (key == kSpatialReuseRadius) mSpatialReuseRadius = value;
        else if (key == kUseDirectLighting) mUseDirectLighting = value;
        else if (key == kSeparatePathBSDF) mStaticParams.separatePathBSDF = value;
        else if (key == kCandidateSamples) mStaticParams.candidateSamples = value;
        else if (key == kTemporalUpdateForDynamicScene) mStaticParams.temporalUpdateForDynamicScene = value;
        else if (key == kEnableRayStats) mEnableRayStats = value;
        else logWarning("Unknown field '" + key + "' in ReSTIRPTPass dictionary");
    }

    // Check for deprecated bounces configuration.
    if (dict.keyExists("maxBounces"))
    {
        logWarning("'maxBounces' is deprecated. Use 'maxSurfaceBounces' instead.");
        if (!dict.keyExists(kMaxSurfaceBounces)) mStaticParams.maxSurfaceBounces = dict["maxBounces"];
    }
    if (dict.keyExists("maxNonSpecularBounces"))
    {
        logWarning("'maxNonSpecularBounces' is deprecated. Use 'maxDiffuseBounces' instead.");
        if (!dict.keyExists(kMaxDiffuseBounces)) mStaticParams.maxDiffuseBounces = dict["maxNonSpecularBounces"];
    }

    // Initialize the other bounce counts to 'maxSurfaceBounces' if they weren't explicitly set.
    if (mStaticParams.maxDiffuseBounces == -1) mStaticParams.maxDiffuseBounces = mStaticParams.maxSurfaceBounces;
    if (mStaticParams.maxSpecularBounces == -1) mStaticParams.maxSpecularBounces = mStaticParams.maxSurfaceBounces;
    if (mStaticParams.maxTransmissionBounces == -1) mStaticParams.maxTransmissionBounces = mStaticParams.maxSurfaceBounces;

    bool maxSurfaceBouncesNeedsAdjustment =
        mStaticParams.maxSurfaceBounces < mStaticParams.maxDiffuseBounces ||
        mStaticParams.maxSurfaceBounces < mStaticParams.maxSpecularBounces ||
        mStaticParams.maxSurfaceBounces < mStaticParams.maxTransmissionBounces;

    // Show a warning if maxSurfaceBounces will be adjusted in validateOptions().
    if ((dict.keyExists("maxSurfaceBounces") || dict.keyExists("maxBounces")) && maxSurfaceBouncesNeedsAdjustment)
    {
        logWarning("'maxSurfaceBounces' is set lower than 'maxDiffuseBounces', 'maxSpecularBounces' or 'maxTransmissionBounces' and will be increased.");
    }

    // Show a warning for deprecated 'useNestedDielectrics'.
    if (dict.keyExists("useNestedDielectrics"))
    {
        logWarning("'useNestedDielectrics' is deprecated. Support for nested dielectrics is always enabled now.");
    }

    return needToReset;
}

void ReSTIRPTPass::validateOptions()
{
    if (mParams.specularRoughnessThreshold < 0.f || mParams.specularRoughnessThreshold > 1.f)
    {
        logError("'specularRoughnessThreshold' has invalid value. Clamping to range [0,1].");
        mParams.specularRoughnessThreshold = clamp(mParams.specularRoughnessThreshold, 0.f, 1.f);
    }

    // Static parameters.
    if (mStaticParams.samplesPerPixel < 1 || mStaticParams.samplesPerPixel > kMaxSamplesPerPixel)
    {
        logError("'samplesPerPixel' must be in the range [1, " + std::to_string(kMaxSamplesPerPixel) + "]. Clamping to this range.");
        mStaticParams.samplesPerPixel = std::clamp(mStaticParams.samplesPerPixel, 1u, kMaxSamplesPerPixel);
    }

    auto clampBounces = [](uint32_t& bounces, const std::string& name)
    {
        if (bounces > kMaxBounces)
        {
            logError("'" + name + "' exceeds the maximum supported bounces. Clamping to " + std::to_string(kMaxBounces));
            bounces = kMaxBounces;
        }
    };

    clampBounces(mStaticParams.maxSurfaceBounces, kMaxSurfaceBounces);
    clampBounces(mStaticParams.maxDiffuseBounces, kMaxDiffuseBounces);
    clampBounces(mStaticParams.maxSpecularBounces, kMaxSpecularBounces);
    clampBounces(mStaticParams.maxTransmissionBounces, kMaxTransmissionBounces);

    // Make sure maxSurfaceBounces is at least as many as any of diffuse, specular or transmission.
    uint32_t minSurfaceBounces = std::max(mStaticParams.maxDiffuseBounces, std::max(mStaticParams.maxSpecularBounces, mStaticParams.maxTransmissionBounces));
    mStaticParams.maxSurfaceBounces = std::max(mStaticParams.maxSurfaceBounces, minSurfaceBounces);

    if (mStaticParams.maxTransmissionReflectionDepth > mStaticParams.maxTransmissionBounces)
    {
        logWarning("'maxTransmissionReflectionDepth' exceeds `maxTransmissionBounces`. Clamping to " + std::to_string(mStaticParams.maxTransmissionBounces));
        mStaticParams.maxTransmissionReflectionDepth = mStaticParams.maxTransmissionBounces;
    }

    if (mStaticParams.maxTransmissionRefractionDepth > mStaticParams.maxTransmissionBounces)
    {
        logWarning("'maxTransmissionRefractionDepth' exceeds `maxTransmissionBounces`. Clamping to " + std::to_string(mStaticParams.maxTransmissionBounces));
        mStaticParams.maxTransmissionRefractionDepth = mStaticParams.maxTransmissionBounces;
    }

    if (mStaticParams.primaryLodMode == TexLODMode::RayCones)
    {
        logError("Unsupported tex lod mode. Defaulting to Mip0.");
        mStaticParams.primaryLodMode = TexLODMode::Mip0;
    }
}

Dictionary ReSTIRPTPass::getScriptingDictionary()
{
    if (auto lightBVHSampler = std::dynamic_pointer_cast<LightBVHSampler>(mpEmissiveSampler))
    {
        mLightBVHOptions = lightBVHSampler->getOptions();
    }

    Dictionary d;
    d[kSamplesPerPixel] = mStaticParams.samplesPerPixel;
    d[kMaxSurfaceBounces] = mStaticParams.maxSurfaceBounces;
    d[kMaxDiffuseBounces] = mStaticParams.maxDiffuseBounces;
    d[kMaxSpecularBounces] = mStaticParams.maxSpecularBounces;
    d[kMaxTransmissionBounces] = mStaticParams.maxTransmissionBounces;
    d[kAdjustShadingNormals] = mStaticParams.adjustShadingNormals;
    d[kLODBias] = mParams.lodBias;
    d[kSampleGenerator] = mStaticParams.sampleGenerator;
    if (mParams.useFixedSeed) d[kFixedSeed] = mParams.fixedSeed;
    d[kUseBSDFSampling] = mStaticParams.useBSDFSampling;
    d[kUseNEE] = mStaticParams.useNEE;
    d[kUseMIS] = mStaticParams.useMIS;
    d[kUseRussianRoulette] = mStaticParams.useRussianRoulette;
    d[kUseAlphaTest] = mStaticParams.useAlphaTest;
    d[kMaxNestedMaterials] = mStaticParams.maxNestedMaterials;
    d[kUseLightsInDielectricVolumes] = mStaticParams.useLightsInDielectricVolumes;
    d[kLimitTransmission] = mStaticParams.limitTransmission;
    d[kMaxTransmissionReflectionDepth] = mStaticParams.maxTransmissionReflectionDepth;
    d[kMaxTransmissionRefractionDepth] = mStaticParams.maxTransmissionRefractionDepth;
    d[kDisableCaustics] = mStaticParams.disableCaustics;
    d[kSpecularRoughnessThreshold] = mParams.specularRoughnessThreshold;
    d[kDisableDirectIllumination] = mStaticParams.disableDirectIllumination;
    d[kPrimaryLodMode] = mStaticParams.primaryLodMode;
    d[kColorFormat] = mStaticParams.colorFormat;
    d[kMISHeuristic] = mStaticParams.misHeuristic;
    d[kMISPowerExponent] = mStaticParams.misPowerExponent;
    d[kEmissiveSampler] = mStaticParams.emissiveSampler;
    if (mStaticParams.emissiveSampler == EmissiveLightSamplerType::LightBVH) d[kLightBVHOptions] = mLightBVHOptions;
    d[kSpatialMisKind] = mStaticParams.spatialMisKind;
    d[kTemporalMisKind] = mStaticParams.temporalMisKind;
    d[kShiftStrategy] = mStaticParams.shiftStrategy;
    d[kRejectShiftBasedOnJacobian] = mParams.rejectShiftBasedOnJacobian;
    d[kJacobianRejectionThreshold] = mParams.jacobianRejectionThreshold;
    d[kNearFieldDistance] = mParams.nearFieldDistance;
    d[kTemporalHistoryLength] = mTemporalHistoryLength;
    d[kUseMaxHistory] = mUseMaxHistory;
    d[kSeedOffset] = mSeedOffset;
    d[kEnableTemporalReuse] = mEnableSpatialReuse;
    d[kEnableSpatialReuse] = mEnableTemporalReuse;
    d[kNumSpatialRounds] = mNumSpatialRounds;
    d[kPathSamplingMode] = mStaticParams.pathSamplingMode;
    d[kLocalStrategyType] = mParams.localStrategyType;
    d[kEnableTemporalReprojection] = mEnableTemporalReprojection;
    d[kNoResamplingForTemporalReuse] = mNoResamplingForTemporalReuse;
    d[kSpatialNeighborCount] = mSpatialNeighborCount;
    d[kFeatureBasedRejection] = mFeatureBasedRejection;
    d[kSpatialReusePattern] = mSpatialReusePattern;
    d[kSmallWindowRestirWindowRadius] = mSmallWindowRestirWindowRadius;
    d[kSpatialReuseRadius] = mSpatialReuseRadius;
    d[kUseDirectLighting] = mUseDirectLighting;
    d[kSeparatePathBSDF] = mStaticParams.separatePathBSDF;
    d[kCandidateSamples] = mStaticParams.candidateSamples;
    d[kTemporalUpdateForDynamicScene] = mStaticParams.temporalUpdateForDynamicScene;
    d[kEnableRayStats] = mEnableRayStats;
    // Denoising parameters
    d[kUseNRDDemodulation] = mStaticParams.useNRDDemodulation;

    return d;
}

Dictionary ReSTIRPTPass::getSpecializedScriptingDictionary()
{
    Dictionary d;
    d[kMaxSurfaceBounces] = mStaticParams.maxSurfaceBounces;
    d[kSpatialMisKind] = mStaticParams.spatialMisKind;
    d[kTemporalMisKind] = mStaticParams.temporalMisKind;
    d[kShiftStrategy] = mStaticParams.shiftStrategy;

    return d;
}

RenderPassReflection ReSTIRPTPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    addRenderPassInputs(reflector, kInputChannels);
    addRenderPassOutputs(reflector, kOutputChannels);
    return reflector;
}

void ReSTIRPTPass::compile(RenderContext* pContext, const CompileData& compileData)
{
    mParams.frameDim = compileData.defaultTexDims;
    if (mParams.frameDim.x > kMaxFrameDimension || mParams.frameDim.y > kMaxFrameDimension)
    {
        logError("Frame dimensions up to " + std::to_string(kMaxFrameDimension) + " pixels width/height are supported.");
    }

    // Tile dimensions have to be powers-of-two.
    assert(isPowerOf2(kScreenTileDim.x) && isPowerOf2(kScreenTileDim.y));
    assert(kScreenTileDim.x == (1 << kScreenTileBits.x) && kScreenTileDim.y == (1 << kScreenTileBits.y));
    mParams.screenTiles = div_round_up(mParams.frameDim, kScreenTileDim);

    mVarsChanged = true;
}

void ReSTIRPTPass::setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene)
{
    mpScene = pScene;
    mParams.frameCount = 0;

    resetLighting();

    if (mpScene)
    {
        if (is_set(pScene->getPrimitiveTypes(), PrimitiveTypeFlags::Custom)) logError("This render pass does not support custom primitives.");

        // check if the scene is dynamic
        bool enableRobustSettingsByDefault = mpScene->hasAnimation() && mpScene->isAnimated();
        mParams.rejectShiftBasedOnJacobian = enableRobustSettingsByDefault;
        mStaticParams.temporalUpdateForDynamicScene = enableRobustSettingsByDefault;

        // Prepare our programs for the scene.
        Shader::DefineList defines = mpScene->getSceneDefines();

        mpGeneratePaths->getProgram()->addDefines(defines);
        mpTracePass->getProgram()->addDefines(defines);
        mpReflectTypes->getProgram()->addDefines(defines);

        mpSpatialPathRetracePass->getProgram()->addDefines(defines);
        mpTemporalPathRetracePass->getProgram()->addDefines(defines);

        mpSpatialReusePass->getProgram()->addDefines(defines);
        mpTemporalReusePass->getProgram()->addDefines(defines);
        mpComputePathReuseMISWeightsPass->getProgram()->addDefines(defines);

        validateOptions();

        mRecompile = true;
    }

    mOptionsChanged = true;
}

void ReSTIRPTPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (!beginFrame(pRenderContext, renderData)) return;
    renderData.getDictionary()["enableScreenSpaceReSTIR"] = mUseDirectLighting;

    bool skipTemporalReuse = mReservoirFrameCount == 0;
    if (mStaticParams.pathSamplingMode != PathSamplingMode::ReSTIR) mStaticParams.candidateSamples = 1;
    if (mStaticParams.pathSamplingMode == PathSamplingMode::PathReuse)
    {
        mStaticParams.shiftStrategy = ShiftMapping::Reconnection;
        mEnableSpatialReuse = true;
    }
    if (mStaticParams.shiftStrategy == ShiftMapping::Hybrid)
    {
        // the ray tracing pass happens before spatial/temporal reuse,
        // so currently hybrid shift is only implemented for Pairwise and Talbot
        mStaticParams.spatialMisKind = ReSTIRMISKind::Pairwise;
        mStaticParams.temporalMisKind = ReSTIRMISKind::Talbot;
    }

    uint32_t numPasses = mStaticParams.pathSamplingMode == PathSamplingMode::PathTracing ? 1 : mStaticParams.samplesPerPixel;

    for (uint32_t restir_i = 0; restir_i < numPasses; restir_i++)
    {
        {
            // Update shader program specialization.
            updatePrograms();

            // Prepare resources.
            prepareResources(pRenderContext, renderData);

            // Prepare the path tracer parameter block.
            // This should be called after all resources have been created.
            preparePathTracer(renderData);

            // Reset atomic counters.

            // Clear time output texture.

            if (const auto& texture = renderData[kOutputTime])
            {
                pRenderContext->clearUAV(texture->getUAV().get(), uint4(0));
            }

            {
                assert(mpCounters);
                pRenderContext->clearUAV(mpCounters->getUAV().get(), uint4(0));

                mpPathTracerBlock->getRootVar()["gSppId"] = restir_i;
                mpPathTracerBlock->getRootVar()["gNumSpatialRounds"] = mNumSpatialRounds;

                if (restir_i == 0)
                    // Generate paths at primary hits.
                    generatePaths(pRenderContext, renderData, 0);

                // Launch main trace pass.
                tracePass(pRenderContext, renderData, mpTracePass, "tracePass", 0);
            }
        }

        if (mStaticParams.pathSamplingMode != PathSamplingMode::PathTracing)
        {
            // Launch restir merge pass.
            if (mStaticParams.pathSamplingMode == PathSamplingMode::ReSTIR)
            {
                if (mEnableTemporalReuse && !skipTemporalReuse)
                {
                    if (mStaticParams.shiftStrategy == ShiftMapping::Hybrid)
                        PathRetracePass(pRenderContext, restir_i, renderData, true, 0);
                    // a separate pass to trace rays for hybrid shift/random number replay
                    PathReusePass(pRenderContext, restir_i, renderData, true, 0, !mEnableSpatialReuse);
                }
            }
            else if (mStaticParams.pathSamplingMode == PathSamplingMode::PathReuse)
            {
                PathReusePass(pRenderContext, restir_i, renderData, false, -1, false);
            }

            if (mEnableSpatialReuse)
            {
                // multiple rounds?
                for (int spatialRoundId = 0; spatialRoundId < mNumSpatialRounds; spatialRoundId++)
                {
                    // a separate pass to trace rays for hybrid shift/random number replay
                    if (mStaticParams.shiftStrategy == ShiftMapping::Hybrid)
                        PathRetracePass(pRenderContext, restir_i, renderData, false, spatialRoundId);
                    PathReusePass(pRenderContext, restir_i, renderData, false, spatialRoundId, spatialRoundId == mNumSpatialRounds - 1);
                }
            }

            if (restir_i == numPasses - 1)
                mReservoirFrameCount++; // mark as at least one temporally reused frame

            if (mEnableTemporalReuse && mStaticParams.pathSamplingMode == PathSamplingMode::ReSTIR)
            {
                if ((!mEnableSpatialReuse || mNumSpatialRounds % 2 == 0))
                    pRenderContext->copyResource(mpTemporalReservoirs[restir_i].get(), mpOutputReservoirs.get());
                if (restir_i == numPasses - 1)
                    pRenderContext->copyResource(mpTemporalVBuffer.get(), renderData[kInputVBuffer].get());
            }
        }
        mParams.seed++;
    }

    mParams.frameCount++;

    endFrame(pRenderContext, renderData);
}

void ReSTIRPTPass::renderUI(Gui::Widgets& widget)
{
    bool dirty = false;

    // Rendering options.
    dirty |= renderRenderingUI(widget);

    // Stats and debug options.
    dirty |= renderStatsUI(widget);

    dirty |= renderDebugUI(widget);

    if (dirty)
    {
        validateOptions();
        mOptionsChanged = true;
    }
}

Texture::SharedPtr ReSTIRPTPass::createNeighborOffsetTexture(uint32_t sampleCount)
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

bool ReSTIRPTPass::renderRenderingUI(Gui::Widgets& widget)
{
    bool dirty = false;

    if (mpScene && mpScene->hasAnimation())
    {
        if (gpFramework->getGlobalClock().isPaused())
        {
            if (widget.button("Resume Animation"))
                gpFramework->getGlobalClock().play();
        }
        else
        {
            if (widget.button("Pause Animation"))
                gpFramework->getGlobalClock().pause();
        }
    }

    dirty |= widget.checkbox("Direct lighting (ReSTIR DI)", mUseDirectLighting);

    bool pathSamplingModeChanged = widget.dropdown("Path Sampling Mode", kPathSamplingModeList, reinterpret_cast<uint32_t&>(mStaticParams.pathSamplingMode));
    if (pathSamplingModeChanged)
    {
        if (mStaticParams.pathSamplingMode == PathSamplingMode::PathReuse)
        {
            mStaticParams.shiftStrategy = ShiftMapping::Reconnection;
            mStaticParams.separatePathBSDF = false;
        }
        else mStaticParams.separatePathBSDF = true;
    }

    if (auto group = widget.group("Path Reuse Controls", true))
    {
        dirty |= pathSamplingModeChanged;

        if (mStaticParams.pathSamplingMode == PathSamplingMode::ReSTIR)
        {
            if (widget.button("Clean Reservoirs"))
            {
                mReservoirFrameCount = 0;
            }

            dirty |= widget.var("Candidate Samples", mStaticParams.candidateSamples, 1u, 64u);
            widget.tooltip("Number candidate samples for ReSTIR PT.\n");

            if (auto group = widget.group("Shift Mapping Options", true))
            {
                if (widget.dropdown("Shift Mapping", kShiftMappingList, reinterpret_cast<uint32_t&>(mStaticParams.shiftStrategy)))
                {
                    dirty = true;
                }

                dirty |= widget.checkbox("Reject Shift based on Jacobian (unbiased)", mParams.rejectShiftBasedOnJacobian);

                if (mParams.rejectShiftBasedOnJacobian)
                {
                    dirty |= widget.var("Shift rejection jacobian threshold", mParams.jacobianRejectionThreshold, 0.f, 100.f);
                }
            }
        }

        if (mStaticParams.pathSamplingMode == PathSamplingMode::ReSTIR && mStaticParams.shiftStrategy == ShiftMapping::Hybrid)
        {
            if (auto group = widget.group("Local Strategies", true))
            {
                bool enableRoughnessCondition = mParams.localStrategyType & (uint32_t)LocalStrategy::RoughnessCondition;
                bool enableDistanceCondition = mParams.localStrategyType & (uint32_t)LocalStrategy::DistanceCondition;

                dirty |= widget.checkbox("Roughness Condition", enableRoughnessCondition);
                dirty |= widget.checkbox("Distance Condition", enableDistanceCondition);

                if (dirty)
                {
                    mParams.localStrategyType = enableDistanceCondition << ((uint32_t)LocalStrategy::DistanceCondition - 1) | enableRoughnessCondition << ((uint32_t)LocalStrategy::RoughnessCondition - 1);
                }
            }

            if (auto group = widget.group("Classification thresholds", true))
            {
                dirty |= widget.var("Near field distance", mParams.nearFieldDistance, 0.f, 100.f);
                dirty |= widget.var("Specular roughness threshold", mParams.specularRoughnessThreshold, 0.f, 1.f);
            }
        }

        if (mStaticParams.pathSamplingMode == PathSamplingMode::ReSTIR)
        {
            dirty |= widget.checkbox("Spatial Reuse", mEnableSpatialReuse);
            dirty |= widget.checkbox("Temporal Reuse", mEnableTemporalReuse);
        }

        if (mStaticParams.pathSamplingMode == PathSamplingMode::PathReuse)
        {
            dirty |= widget.dropdown("Bekaert-Style Path Reuse Pattern", kPathReusePatternList, reinterpret_cast<uint32_t&>(mPathReusePattern));
        }
        else if (mStaticParams.pathSamplingMode == PathSamplingMode::ReSTIR && mEnableSpatialReuse)
        {
            if (auto group = widget.group("Spatial reuse controls", true))
            {
                dirty |= widget.var("Num Spatial Rounds", mNumSpatialRounds, 1, 5);
                dirty |= widget.dropdown("Spatial Reuse Pattern", kSpatialReusePatternList, reinterpret_cast<uint32_t&>(mSpatialReusePattern));
                dirty |= widget.checkbox("Feature-based rejection", mFeatureBasedRejection);

                if (SpatialReusePattern(mSpatialReusePattern) == SpatialReusePattern::SmallWindow)
                {
                    dirty |= widget.var("Window radius", mSmallWindowRestirWindowRadius, 0u, 32u);
                }
                else
                {
                    dirty |= widget.var("Spatial Neighbor Count", mSpatialNeighborCount, 0, 6);
                    dirty |= widget.var("Spatial Reuse Radius", mSpatialReuseRadius, 0.f, 100.f);
                }

                dirty |= widget.dropdown("Spatial Resampling MIS Kind", kReSTIRMISList, reinterpret_cast<uint32_t&>(mStaticParams.spatialMisKind));
                widget.tooltip("Current implementation only support pairwise MIS for hybird shift.\n");
            }
        }

        if (mStaticParams.pathSamplingMode == PathSamplingMode::ReSTIR && mEnableTemporalReuse)
        {
            if (auto group = widget.group("Temporal reuse controls", true))
            {
                dirty |= widget.var("Temporal History Length", mTemporalHistoryLength, 0, 100);
                dirty |= widget.checkbox("Use M capping", mUseMaxHistory);
                dirty |= widget.checkbox("Temporal Reprojection", mEnableTemporalReprojection);
                dirty |= widget.checkbox("Temporal Update for Dynamic Scenes", mStaticParams.temporalUpdateForDynamicScene);
                widget.tooltip("Resample cached radiance in reconnection vertex in temporal reservoir for dynamic scenes (eliminate lags).");
                dirty |= widget.checkbox("Disable Resampling in Temporal Reuse", mNoResamplingForTemporalReuse);
                dirty |= widget.dropdown("Temporal Resampling MIS Kind", kReSTIRMISList2, reinterpret_cast<uint32_t&>(mStaticParams.temporalMisKind));
                widget.tooltip("Current implementation only support Talbot MIS for hybird shift.\n");
            }
        }
    }

    if (auto group = widget.group("Shared Path Sampler Options", true))
    {

        dirty |= widget.var("Samples/pixel", mStaticParams.samplesPerPixel, 1u, kMaxSamplesPerPixel);

        widget.tooltip("Number of samples per pixel. One path is traced for each sample.\n");

        dirty |= widget.checkbox("Use Sampled BSDFs", mStaticParams.separatePathBSDF);
        widget.tooltip("Control whether to use mixture BSDF or sampled BSDF in path tracing/path reuse.\n");

        if (widget.var("Max bounces (override all)", mStaticParams.maxSurfaceBounces, 0u, kMaxBounces))
        {
            // Allow users to change the max surface bounce parameter in the UI to clamp all other surface bounce parameters.
            mStaticParams.maxDiffuseBounces = mStaticParams.maxSurfaceBounces;
            mStaticParams.maxSpecularBounces = mStaticParams.maxSurfaceBounces;
            mStaticParams.maxTransmissionBounces = mStaticParams.maxSurfaceBounces;
            dirty = true;
        }

        if (widget.var("Max surface bounces", mStaticParams.maxSurfaceBounces, 0u, kMaxBounces))
        {
            // Allow users to change the max surface bounce parameter in the UI to clamp all other surface bounce parameters.
            mStaticParams.maxDiffuseBounces = std::min(mStaticParams.maxDiffuseBounces, mStaticParams.maxSurfaceBounces);
            mStaticParams.maxSpecularBounces = std::min(mStaticParams.maxSpecularBounces, mStaticParams.maxSurfaceBounces);
            mStaticParams.maxTransmissionBounces = std::min(mStaticParams.maxTransmissionBounces, mStaticParams.maxSurfaceBounces);
            dirty = true;
        }
        widget.tooltip("Maximum number of surface bounces (diffuse + specular + transmission).\n"
            "Note that specular reflection events from a material with a roughness greater than specularRoughnessThreshold are also classified as diffuse events.");

        dirty |= widget.var("Max diffuse bounces", mStaticParams.maxDiffuseBounces, 0u, kMaxBounces);
        widget.tooltip("Maximum number of diffuse bounces.\n0 = direct only\n1 = one indirect bounce etc.");

        dirty |= widget.var("Max specular bounces", mStaticParams.maxSpecularBounces, 0u, kMaxBounces);
        widget.tooltip("Maximum number of specular bounces.\n0 = direct only\n1 = one indirect bounce etc.");

        dirty |= widget.var("Max transmission bounces", mStaticParams.maxTransmissionBounces, 0u, kMaxBounces);
        widget.tooltip("Maximum number of transmission bounces.\n0 = no transmission\n1 = one transmission bounce etc.");

        // Sampling options.

        if (widget.dropdown("Sample generator", SampleGenerator::getGuiDropdownList(), mStaticParams.sampleGenerator))
        {
            mpSampleGenerator = SampleGenerator::create(mStaticParams.sampleGenerator);
            dirty = true;
        }

        dirty |= widget.checkbox("BSDF importance sampling", mStaticParams.useBSDFSampling);
        widget.tooltip("BSDF importance sampling should normally be enabled.\n\n"
            "If disabled, cosine-weighted hemisphere sampling is used for debugging purposes");

        dirty |= widget.checkbox("Disable direct illumination", mStaticParams.disableDirectIllumination);
        widget.tooltip("If enabled, incoming radiance is collected starting from first order indirect.");

        dirty |= widget.checkbox("Russian roulette", mStaticParams.useRussianRoulette);
        widget.tooltip("Use russian roulette to terminate low throughput paths.");

        dirty |= widget.checkbox("Next-event estimation (NEE)", mStaticParams.useNEE);
        widget.tooltip("Use next-event estimation.\nThis option enables direct illumination sampling at each path vertex.");

        if (mStaticParams.useNEE)
        {
            dirty |= widget.checkbox("Multiple importance sampling (MIS)", mStaticParams.useMIS);
            widget.tooltip("When enabled, BSDF sampling is combined with light sampling for the environment map and emissive lights.\n"
                "Note that MIS has currently no effect on analytic lights.");

            if (mStaticParams.useMIS)
            {
                dirty |= widget.dropdown("MIS heuristic", kMISHeuristicList, reinterpret_cast<uint32_t&>(mStaticParams.misHeuristic));

                if (mStaticParams.misHeuristic == MISHeuristic::PowerExp)
                {
                    dirty |= widget.var("MIS power exponent", mStaticParams.misPowerExponent, 0.01f, 10.f);
                }
            }

            if (mpScene && mpScene->useEmissiveLights())
            {
                if (auto group = widget.group("Emissive sampler"))
                {
                    if (widget.dropdown("Emissive sampler", kEmissiveSamplerList, (uint32_t&)mStaticParams.emissiveSampler))
                    {
                        resetLighting();
                        dirty = true;
                    }
                    widget.tooltip("Selects which light sampler to use for importance sampling of emissive geometry.", true);

                    if (mpEmissiveSampler)
                    {
                        if (mpEmissiveSampler->renderUI(group)) mOptionsChanged = true;
                    }
                }
            }
        }
    }

    if (auto group = widget.group("Material controls"))
    {
        dirty |= widget.checkbox("Alpha test", mStaticParams.useAlphaTest);
        widget.tooltip("Use alpha testing on non-opaque triangles.");

        dirty |= widget.checkbox("Adjust shading normals on secondary hits", mStaticParams.adjustShadingNormals);
        widget.tooltip("Enables adjustment of the shading normals to reduce the risk of black pixels due to back-facing vectors.\nDoes not apply to primary hits which is configured in GBuffer.", true);

        dirty |= widget.var("Max nested materials", mStaticParams.maxNestedMaterials, 2u, 4u);
        widget.tooltip("Maximum supported number of nested materials.");

        dirty |= widget.checkbox("Use deterministic BSDF evaluations", mStaticParams.useDeterministicBSDF);
        widget.tooltip("If disabled, BSDF evaluations of BSDF-sampled directions are only correct on expectation.", true);

        dirty |= widget.checkbox("Use lights in dielectric volumes", mStaticParams.useLightsInDielectricVolumes);
        widget.tooltip("Use lights inside of volumes (transmissive materials). We typically don't want this because lights are occluded by the interface.");

        dirty |= widget.checkbox("Limit transmission", mStaticParams.limitTransmission);
        widget.tooltip("Limit specular transmission by handling reflection/refraction events only up to a given transmission depth.");

        if (mStaticParams.limitTransmission)
        {
            dirty |= widget.var("Max transmission reflection depth", mStaticParams.maxTransmissionReflectionDepth, 0u, kMaxBounces);
            widget.tooltip("Maximum transmission depth at which to sample specular reflection.\n"
                "0: Reflection is never sampled.\n"
                "1: Reflection is only sampled on primary hits.\n"
                "N: Reflection is only sampled on the first N hits.");

            dirty |= widget.var("Max transmission refraction depth", mStaticParams.maxTransmissionRefractionDepth, 0u, kMaxBounces);
            widget.tooltip("Maximum transmission depth at which to sample specular refraction (after that, IoR is set to 1).\n"
                "0: Refraction is never sampled.\n"
                "1: Refraction is only sampled on primary hits.\n"
                "N: Refraction is only sampled on the first N hits.");
        }

        dirty |= widget.checkbox("Disable caustics", mStaticParams.disableCaustics);
        widget.tooltip("Disable sampling of caustic light paths (i.e. specular events after diffuse events).");

        dirty |= widget.checkbox("Disable direct illumination", mStaticParams.disableDirectIllumination);
        widget.tooltip("Disable computation of all direct illumination.");

        dirty |= widget.var("TexLOD bias", mParams.lodBias, -16.f, 16.f, 0.01f);
    }

    if (auto group = widget.group("Denoiser options"))
    {
        dirty |= widget.checkbox("Use NRD demodulation", mStaticParams.useNRDDemodulation);
        widget.tooltip("Global switch for NRD demodulation");
    }

    if (auto group = widget.group("Output options"))
    {
        dirty |= widget.dropdown("Color format", kColorFormatList, (uint32_t&)mStaticParams.colorFormat);
        widget.tooltip("Selects the color format used for internal per-sample color and denoiser buffers");
    }

    if (dirty) mRecompile = true;
    return dirty;
}

bool ReSTIRPTPass::renderDebugUI(Gui::Widgets& widget)
{
    bool dirty = false;

    if (auto group = widget.group("Debugging", true))
    {
        dirty |= group.checkbox("Use fixed seed", mParams.useFixedSeed);
        group.tooltip("Forces a fixed random seed for each frame.\n\n"
            "This should produce exactly the same image each frame, which can be useful for debugging.");
        if (mParams.useFixedSeed)
        {
            dirty |= group.var("Seed", mParams.fixedSeed);
        }

        mpPixelDebug->renderUI(group);
    }

    return dirty;
}

bool ReSTIRPTPass::renderStatsUI(Gui::Widgets& widget)
{
    bool dirty = false;
    if (auto g = widget.group("Statistics"))
    {
        // Show ray stats
        dirty |= mpPixelStats->renderUI(g);
    }
    return dirty;
}

bool ReSTIRPTPass::onMouseEvent(const MouseEvent& mouseEvent)
{
    return mpPixelDebug->onMouseEvent(mouseEvent);
}

void ReSTIRPTPass::updatePrograms()
{
    if (mRecompile == false) return;

    mStaticParams.rcDataOfflineMode = mSpatialNeighborCount > 3 && mStaticParams.shiftStrategy == ShiftMapping::Hybrid;

    auto defines = mStaticParams.getDefines(*this);

    // Update program specialization. This is done through defines in lieu of specialization constants.
    mpGeneratePaths->getProgram()->addDefines(defines);
    mpTracePass->getProgram()->addDefines(defines);
    mpReflectTypes->getProgram()->addDefines(defines);
    mpSpatialPathRetracePass->getProgram()->addDefines(defines);
    mpTemporalPathRetracePass->getProgram()->addDefines(defines);
    mpSpatialReusePass->getProgram()->addDefines(defines);
    mpTemporalReusePass->getProgram()->addDefines(defines);
    mpComputePathReuseMISWeightsPass->getProgram()->addDefines(defines);

    // Recreate program vars. This may trigger recompilation if needed.
    // Note that program versions are cached, so switching to a previously used specialization is faster.
    mpGeneratePaths->setVars(nullptr);
    mpTracePass->setVars(nullptr);
    mpReflectTypes->setVars(nullptr);
    mpSpatialPathRetracePass->setVars(nullptr);
    mpTemporalPathRetracePass->setVars(nullptr);
    mpSpatialReusePass->setVars(nullptr);
    mpTemporalReusePass->setVars(nullptr);
    mpComputePathReuseMISWeightsPass->setVars(nullptr);

    mVarsChanged = true;
    mRecompile = false;
}

void ReSTIRPTPass::prepareResources(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Compute allocation requirements for paths and output samples.
    // Note that the sample buffers are padded to whole tiles, while the max path count depends on actual frame dimension.
    // If we don't have a fixed sample count, assume the worst case.
    uint32_t tileCount = mParams.screenTiles.x * mParams.screenTiles.y;
    const uint32_t reservoirCount = tileCount * kScreenTileDim.x * kScreenTileDim.y;
    const uint32_t screenPixelCount = mParams.frameDim.x * mParams.frameDim.y;
    const uint32_t sampleCount = reservoirCount; // we are effectively only using 1spp for ReSTIR

    auto var = mpReflectTypes->getRootVar();

    if (mStaticParams.pathSamplingMode != PathSamplingMode::PathTracing)
    {

        if (mStaticParams.shiftStrategy == ShiftMapping::Hybrid && (!mReconnectionDataBuffer ||
            mStaticParams.rcDataOfflineMode && mReconnectionDataBuffer->getElementSize() != 512 ||
            !mStaticParams.rcDataOfflineMode && mReconnectionDataBuffer->getElementSize() != 256))
        {
            mReconnectionDataBuffer = Buffer::createStructured(var["reconnectionDataBuffer"], reservoirCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
            //printf("rcDataSize size: %d\n", mReconnectionDataBuffer->getElementSize());
        }
        if (mStaticParams.shiftStrategy != ShiftMapping::Hybrid)
            mReconnectionDataBuffer = nullptr;

        uint32_t baseReservoirSize = 88;
        uint32_t pathTreeReservoirSize = 128;

        if (mpOutputReservoirs &&
            (mStaticParams.pathSamplingMode == PathSamplingMode::PathReuse && mpOutputReservoirs->getElementSize() != pathTreeReservoirSize ||
                mStaticParams.pathSamplingMode != PathSamplingMode::PathReuse && mpOutputReservoirs->getElementSize() != baseReservoirSize ||
                mpTemporalReservoirs.size() != mStaticParams.samplesPerPixel && mStaticParams.pathSamplingMode != PathSamplingMode::PathReuse))
        {
            mpOutputReservoirs = Buffer::createStructured(var["outputReservoirs"], reservoirCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
            //printf("reservoir size: %d\n", mpOutputReservoirs->getElementSize());

            if (mStaticParams.pathSamplingMode != PathSamplingMode::PathReuse)
            {
                mpTemporalReservoirs.resize(mStaticParams.samplesPerPixel);
                for (uint32_t i = 0; i < mStaticParams.samplesPerPixel; i++)
                    mpTemporalReservoirs[i] = Buffer::createStructured(var["outputReservoirs"], reservoirCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
            }
            mVarsChanged = true;
        }

        if (mStaticParams.pathSamplingMode == PathSamplingMode::PathReuse)
        {
            if (!mPathReuseMISWeightBuffer)
            {
                mPathReuseMISWeightBuffer = Buffer::createStructured(var["misWeightBuffer"], reservoirCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
                mVarsChanged = true;
            }
            mpTemporalReservoirs.clear();
        }
        else mPathReuseMISWeightBuffer = nullptr;

        // Allocate path buffers.
        if (!mpOutputReservoirs || reservoirCount != mpOutputReservoirs->getElementCount())
        {
            mpOutputReservoirs = Buffer::createStructured(var["outputReservoirs"], reservoirCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
            //printf("reservoir size: %d\n", mpOutputReservoirs->getElementSize());

            if (mStaticParams.pathSamplingMode == PathSamplingMode::PathReuse)
            {
                mPathReuseMISWeightBuffer = Buffer::createStructured(var["misWeightBuffer"], reservoirCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
            }
            else
            {
                mpTemporalReservoirs.resize(mStaticParams.samplesPerPixel);
                for (uint32_t i = 0; i < mStaticParams.samplesPerPixel; i++)
                    mpTemporalReservoirs[i] = Buffer::createStructured(var["outputReservoirs"], reservoirCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
            }
            mVarsChanged = true;
        }
    }

    if (!mpTemporalVBuffer || mpTemporalVBuffer->getHeight() != mParams.frameDim.y || mpTemporalVBuffer->getWidth() != mParams.frameDim.x)
    {
        mpTemporalVBuffer = Texture::create2D(mParams.frameDim.x, mParams.frameDim.y, mpScene->getHitInfo().getFormat(), 1, 1);
    }
}


void ReSTIRPTPass::setNRDData(const ShaderVar& var, const RenderData& renderData) const
{
    var["primaryHitEmission"] = renderData[kOutputNRDEmission]->asTexture();
    var["primaryHitDiffuseReflectance"] = renderData[kOutputNRDDiffuseReflectance]->asTexture();
    var["primaryHitSpecularReflectance"] = renderData[kOutputNRDSpecularReflectance]->asTexture();
}

void ReSTIRPTPass::preparePathTracer(const RenderData& renderData)
{
    // Create path tracer parameter block if needed.
    if (!mpPathTracerBlock || mVarsChanged)
    {
        auto reflector = mpTracePass->getProgram()->getReflector()->getParameterBlock("gPathTracer");
        mpPathTracerBlock = ParameterBlock::create(reflector);
        assert(mpPathTracerBlock);
        mVarsChanged = true;
    }

    // Bind resources.
    auto var = mpPathTracerBlock->getRootVar();
    setShaderData(var, renderData, true, false);
    var["outputReservoirs"] = mpOutputReservoirs;
    var["directLighting"] = renderData[kInputDirectLighting]->asTexture();
}

void ReSTIRPTPass::resetLighting()
{
    // Retain the options for the emissive sampler.
    if (auto lightBVHSampler = std::dynamic_pointer_cast<LightBVHSampler>(mpEmissiveSampler))
    {
        mLightBVHOptions = lightBVHSampler->getOptions();
    }
    mpEmissiveSampler = nullptr;
    mpEnvMapSampler = nullptr;
    mRecompile = true;
}

void ReSTIRPTPass::prepareMaterials(RenderContext* pRenderContext)
{
    // This functions checks for material changes and performs any necessary update.
    // For now all we need to do is to trigger a recompile so that the right defines get set.
    // In the future, we might want to do additional material-specific setup here.

    if (is_set(mpScene->getUpdates(), Scene::UpdateFlags::MaterialsChanged))
    {
        mRecompile = true;
    }
}
bool ReSTIRPTPass::prepareLighting(RenderContext* pRenderContext)
{
    bool lightingChanged = false;

    if (is_set(mpScene->getUpdates(), Scene::UpdateFlags::RenderSettingsChanged))
    {
        lightingChanged = true;
        mRecompile = true;
    }

    if (is_set(mpScene->getUpdates(), Scene::UpdateFlags::EnvMapChanged))
    {
        mpEnvMapSampler = nullptr;
        lightingChanged = true;
        mRecompile = true;
    }

    if (mpScene->useEnvLight())
    {
        if (!mpEnvMapSampler)
        {
            mpEnvMapSampler = EnvMapSampler::create(pRenderContext, mpScene->getEnvMap());
            lightingChanged = true;
            mRecompile = true;
        }
    }
    else
    {
        if (mpEnvMapSampler)
        {
            mpEnvMapSampler = nullptr;
            lightingChanged = true;
            mRecompile = true;
        }
    }

    // Request the light collection if emissive lights are enabled.
    if (mpScene->getRenderSettings().useEmissiveLights)
    {
        mpScene->getLightCollection(pRenderContext);
    }

    if (mpScene->useEmissiveLights())
    {
        if (!mpEmissiveSampler)
        {
            const auto& pLights = mpScene->getLightCollection(pRenderContext);
            assert(pLights && pLights->getActiveLightCount() > 0);
            assert(!mpEmissiveSampler);

            switch (mStaticParams.emissiveSampler)
            {
            case EmissiveLightSamplerType::Uniform:
                mpEmissiveSampler = EmissiveUniformSampler::create(pRenderContext, mpScene);
                break;
            case EmissiveLightSamplerType::LightBVH:
                mpEmissiveSampler = LightBVHSampler::create(pRenderContext, mpScene, mLightBVHOptions);
                break;
            case EmissiveLightSamplerType::Power:
                mpEmissiveSampler = EmissivePowerSampler::create(pRenderContext, mpScene);
                break;
            default:
                logError("Unknown emissive light sampler type");
            }
            lightingChanged = true;
            mRecompile = true;
        }
    }
    else
    {
        if (mpEmissiveSampler)
        {
            // Retain the options for the emissive sampler.
            if (auto lightBVHSampler = std::dynamic_pointer_cast<LightBVHSampler>(mpEmissiveSampler))
            {
                mLightBVHOptions = lightBVHSampler->getOptions();
            }

            mpEmissiveSampler = nullptr;
            lightingChanged = true;
            mRecompile = true;
        }
    }

    if (mpEmissiveSampler)
    {
        lightingChanged |= mpEmissiveSampler->update(pRenderContext);
        auto defines = mpEmissiveSampler->getDefines();
        if (mpTracePass->getProgram()->addDefines(defines)) mRecompile = true;
        if (mpSpatialPathRetracePass->getProgram()->addDefines(defines)) mRecompile = true;
        if (mpTemporalPathRetracePass->getProgram()->addDefines(defines)) mRecompile = true;
        if (mpSpatialReusePass->getProgram()->addDefines(defines)) mRecompile = true;
        if (mpTemporalReusePass->getProgram()->addDefines(defines)) mRecompile = true;
        if (mpComputePathReuseMISWeightsPass->getProgram()->addDefines(defines)) mRecompile = true;
    }

    return lightingChanged;
}

void ReSTIRPTPass::setShaderData(const ShaderVar& var, const RenderData& renderData, bool isPathTracer, bool isPathGenerator) const
{
    // Bind static resources that don't change per frame.
    if (mVarsChanged)
    {
        if (isPathTracer && mpEnvMapSampler) mpEnvMapSampler->setShaderData(var["envMapSampler"]);
    }

    // Bind runtime data.
    var["params"].setBlob(mParams);
    var["vbuffer"] = renderData[kInputVBuffer]->asTexture();
    var["outputColor"] = renderData[kOutputColor]->asTexture();


    if (mOutputNRDData && isPathTracer)
    {
        setNRDData(var["outputNRD"], renderData);
        var["outputNRDDiffuseRadianceHitDist"] = renderData[kOutputNRDDiffuseRadianceHitDist]->asTexture();    ///< Output resolved diffuse color in .rgb and hit distance in .a for NRD. Only valid if kOutputNRDData == true.
        var["outputNRDSpecularRadianceHitDist"] = renderData[kOutputNRDSpecularRadianceHitDist]->asTexture();  ///< Output resolved specular color in .rgb and hit distance in .a for NRD. Only valid if kOutputNRDData == true.
        var["outputNRDResidualRadianceHitDist"] = renderData[kOutputNRDResidualRadianceHitDist]->asTexture();///< Output resolved residual color in .rgb and hit distance in .a for NRD. Only valid if kOutputNRDData == true.
    }

    if (isPathTracer)
    {
        var["isLastRound"] = !mEnableSpatialReuse && !mEnableTemporalReuse;
        var["useDirectLighting"] = mUseDirectLighting;
        var["kUseEnvLight"] = mpScene->useEnvLight();
        var["kUseEmissiveLights"] = mpScene->useEmissiveLights();
        var["kUseAnalyticLights"] = mpScene->useAnalyticLights();
    }
    else if (isPathGenerator)
    {
        var["kUseEnvBackground"] = mpScene->useEnvBackground();
    }

    if (auto outputDebug = var.findMember("outputDebug"); outputDebug.isValid())
    {
        outputDebug = renderData[kOutputDebug]->asTexture(); // Can be nullptr
    }
    if (auto outputTime = var.findMember("outputTime"); outputTime.isValid())
    {
        outputTime = renderData[kOutputTime]->asTexture(); // Can be nullptr
    }

    if (isPathTracer && mpEmissiveSampler)
    {
        // TODO: Do we have to bind this every frame?
        bool success = mpEmissiveSampler->setShaderData(var["emissiveSampler"]);
        if (!success) throw std::exception("Failed to bind emissive light sampler");
    }
}

bool ReSTIRPTPass::beginFrame(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (mOptionsChanged)
    {
        mReservoirFrameCount = 0;
    }
    // Clear outputs that need to be cleared.
    const auto& pOutputDebug = renderData[kOutputDebug]->asTexture();
    if (pOutputDebug) pRenderContext->clearUAV(pOutputDebug->getUAV().get(), float4(0.f));

    if (mpScene == nullptr)
    {
        const auto& pOutputColor = renderData[kOutputColor]->asTexture();
        assert(pOutputColor);
        pRenderContext->clearUAV(pOutputColor->getUAV().get(), float4(0.f));

        return false;
    }

    // Update the env map and emissive sampler to the current frame.
    bool lightingChanged = prepareLighting(pRenderContext);

    // Update refresh flag if changes that affect the output have occured.
    auto& dict = renderData.getDictionary();
    if (mOptionsChanged || lightingChanged)
    {
        auto flags = dict.getValue(kRenderPassRefreshFlags, Falcor::RenderPassRefreshFlags::None);
        if (mOptionsChanged)
        {
            flags |= Falcor::RenderPassRefreshFlags::RenderOptionsChanged;
        }
        if (lightingChanged) flags |= Falcor::RenderPassRefreshFlags::LightingChanged;
        dict[Falcor::kRenderPassRefreshFlags] = flags;
        mOptionsChanged = false;
    }

    // Check if GBuffer has adjusted shading normals enabled.
    bool gbufferAdjustShadingNormals = dict.getValue(Falcor::kRenderPassGBufferAdjustShadingNormals, false);
    if (gbufferAdjustShadingNormals != mGBufferAdjustShadingNormals)
    {
        mGBufferAdjustShadingNormals = gbufferAdjustShadingNormals;
        mRecompile = true;
    }

    // Check if NRD data should be generated.
    mOutputNRDData =
        renderData[kOutputNRDDiffuseRadianceHitDist] != nullptr
        || renderData[kOutputNRDSpecularRadianceHitDist] != nullptr
        || renderData[kOutputNRDResidualRadianceHitDist] != nullptr
        || renderData[kOutputNRDEmission] != nullptr
        || renderData[kOutputNRDDiffuseReflectance] != nullptr
        || renderData[kOutputNRDSpecularReflectance] != nullptr;

    // Check if time data should be generated.
    mOutputTime = renderData[kOutputTime] != nullptr;

    // Enable pixel stats if rayCount or pathLength outputs are connected.
    if (renderData[kOutputRayCount] != nullptr
        || renderData[kOutputPathLength] != nullptr
        || mEnableRayStats)
        mpPixelStats->setEnabled(true);

    mpPixelStats->beginFrame(pRenderContext, renderData.getDefaultTextureDims());
    mpPixelDebug->beginFrame(pRenderContext, renderData.getDefaultTextureDims());

    // Update the random seed.
    int initialShaderPasses = mStaticParams.pathSamplingMode == PathSamplingMode::PathTracing ? 1 : mStaticParams.samplesPerPixel;
    mParams.seed = mParams.useFixedSeed ? mParams.fixedSeed : mSeedOffset + initialShaderPasses * mParams.frameCount;

    return true;
}

void ReSTIRPTPass::endFrame(RenderContext* pRenderContext, const RenderData& renderData)
{
    mpPixelStats->endFrame(pRenderContext);
    mpPixelDebug->endFrame(pRenderContext);

    if (mEnableRayStats)
    {
        PixelStats::Stats stats;
        mpPixelStats->getStats(stats);
        mAccumulatedShadowRayCount += (uint64_t)stats.shadowRays;
        mAccumulatedClosestHitRayCount += (uint64_t)stats.closestHitRays;
        mAccumulatedRayCount += (uint64_t)stats.totalRays;
    }

    auto copyTexture = [pRenderContext](Texture* pDst, const Texture* pSrc)
    {
        if (pDst && pSrc)
        {
            assert(pDst && pSrc);
            assert(pDst->getFormat() == pSrc->getFormat());
            assert(pDst->getWidth() == pSrc->getWidth() && pDst->getHeight() == pSrc->getHeight());
            pRenderContext->copyResource(pDst, pSrc);
        }
        else if (pDst)
        {
            pRenderContext->clearUAV(pDst->getUAV().get(), uint4(0, 0, 0, 0));
        }
    };

    // Copy pixel stats to outputs if available.
    copyTexture(renderData[kOutputRayCount]->asTexture().get(), mpPixelStats->getRayCountTexture(pRenderContext).get());
    copyTexture(renderData[kOutputPathLength]->asTexture().get(), mpPixelStats->getPathLengthTexture().get());

    mVarsChanged = false;
}

void ReSTIRPTPass::generatePaths(RenderContext* pRenderContext, const RenderData& renderData, int sampleId)
{
    PROFILE("generatePaths");

    // Check shader assumptions.
    // We launch one thread group per screen tile, with threads linearly indexed.
    const uint32_t tileSize = kScreenTileDim.x * kScreenTileDim.y;
    assert(kScreenTileDim.x == 16 && kScreenTileDim.y == 16); // TODO: Remove this temporary limitation when Slang bug has been fixed, see comments in shader.
    assert(kScreenTileBits.x <= 4 && kScreenTileBits.y <= 4); // Since we use 8-bit deinterleave.
    assert(mpGeneratePaths->getThreadGroupSize().x == tileSize);
    assert(mpGeneratePaths->getThreadGroupSize().y == 1 && mpGeneratePaths->getThreadGroupSize().z == 1);

    // Additional specialization. This shouldn't change resource declarations.
    mpGeneratePaths->addDefine("OUTPUT_TIME", mOutputTime ? "1" : "0");
    mpGeneratePaths->addDefine("OUTPUT_NRD_DATA", mOutputNRDData ? "1" : "0");

    // Bind resources.
    auto var = mpGeneratePaths->getRootVar()["CB"]["gPathGenerator"];
    setShaderData(var, renderData, false, true);

    mpGeneratePaths["gScene"] = mpScene->getParameterBlock();
    var["gSampleId"] = sampleId;

    // Launch one thread per pixel.
    // The dimensions are padded to whole tiles to allow re-indexing the threads in the shader.
    mpGeneratePaths->execute(pRenderContext, { mParams.screenTiles.x * tileSize, mParams.screenTiles.y, 1u });
}

void ReSTIRPTPass::tracePass(RenderContext* pRenderContext, const RenderData& renderData, const ComputePass::SharedPtr& pass, const std::string& passName, int sampleID)
{
    PROFILE(passName);

    // Additional specialization. This shouldn't change resource declarations.
    bool outputDebug = renderData[kOutputDebug] != nullptr;
    pass->addDefine("OUTPUT_TIME", mOutputTime ? "1" : "0");
    pass->addDefine("OUTPUT_DEBUG", outputDebug ? "1" : "0");
    pass->addDefine("OUTPUT_NRD_DATA", mOutputNRDData ? "1" : "0");

    // Bind global resources.
    auto var = pass->getRootVar();
    mpScene->setRaytracingShaderData(pRenderContext, var);

    if (mVarsChanged) mpSampleGenerator->setShaderData(var);

    mpPixelStats->prepareProgram(pass->getProgram(), var);
    mpPixelDebug->prepareProgram(pass->getProgram(), var);

    // Bind the path tracer.
    var["gPathTracer"] = mpPathTracerBlock;
    var["CB"]["gSampleId"] = sampleID;

    // Launch the threads.
    auto frameDim = renderData.getDefaultTextureDims();
    pass->execute(pRenderContext, uint3(frameDim, 1u));
}

void ReSTIRPTPass::PathReusePass(RenderContext* pRenderContext, uint32_t restir_i, const RenderData& renderData, bool isTemporalReuse, int spatialRoundId, bool isLastRound)
{
    bool isPathReuseMISWeightComputation = spatialRoundId == -1;

    PROFILE(isTemporalReuse ? "temporalReuse" : (isPathReuseMISWeightComputation ? "MISWeightComputation" : "spatialReuse"));

    ComputePass::SharedPtr pass = isPathReuseMISWeightComputation ? mpComputePathReuseMISWeightsPass : (isTemporalReuse ? mpTemporalReusePass : mpSpatialReusePass);

    if (isPathReuseMISWeightComputation)
    {
        spatialRoundId = 0;
        restir_i = 0;
    }

    // Check shader assumptions.
    // We launch one thread group per screen tile, with threads linearly indexed.
    const uint32_t tileSize = kScreenTileDim.x * kScreenTileDim.y;
    assert(kScreenTileDim.x == 16 && kScreenTileDim.y == 16); // TODO: Remove this temporary limitation when Slang bug has been fixed, see comments in shader.
    assert(kScreenTileBits.x <= 4 && kScreenTileBits.y <= 4); // Since we use 8-bit deinterleave.
    assert(pass->getThreadGroupSize().x == 16);
    assert(pass->getThreadGroupSize().y == 16 && pass->getThreadGroupSize().z == 1);

    // Additional specialization. This shouldn't change resource declarations.
    pass->addDefine("OUTPUT_TIME", mOutputTime ? "1" : "0");
    pass->addDefine("TEMPORAL_REUSE", isTemporalReuse ? "1" : "0");
    pass->addDefine("OUTPUT_NRD_DATA", mOutputNRDData ? "1" : "0");

    // Bind resources.
    auto var = pass->getRootVar()["CB"]["gPathReusePass"];

    // TODO: refactor arguments
    setShaderData(var, renderData, false, false);

    var["outputReservoirs"] = spatialRoundId % 2 == 1 ? mpTemporalReservoirs[restir_i] : mpOutputReservoirs;

    if (mStaticParams.pathSamplingMode == PathSamplingMode::PathReuse)
    {
        var["nRooksPattern"] = mNRooksPatternBuffer;
    }

    if (mStaticParams.pathSamplingMode == PathSamplingMode::PathReuse)
        var["misWeightBuffer"] = mPathReuseMISWeightBuffer;
    else if (!isPathReuseMISWeightComputation)
        var["temporalReservoirs"] = spatialRoundId % 2 == 0 ? mpTemporalReservoirs[restir_i] : mpOutputReservoirs;
    var["reconnectionDataBuffer"] = mReconnectionDataBuffer;

    var["gNumSpatialRounds"] = mNumSpatialRounds;

    if (isTemporalReuse)
    {
        var["temporalVbuffer"] = mpTemporalVBuffer;
        var["motionVectors"] = renderData[kInputMotionVectors]->asTexture();
        var["gEnableTemporalReprojection"] = mEnableTemporalReprojection;
        var["gNoResamplingForTemporalReuse"] = mNoResamplingForTemporalReuse;
        if (!mUseMaxHistory) var["gTemporalHistoryLength"] = 1e30f;
        else var["gTemporalHistoryLength"] = (float)mTemporalHistoryLength;
    }
    else
    {
        var["gSpatialReusePattern"] = mStaticParams.pathSamplingMode == PathSamplingMode::PathReuse ? (uint32_t)mPathReusePattern : (uint32_t)mSpatialReusePattern;

        if (!isPathReuseMISWeightComputation)
        {
            var["gNeighborCount"] = mSpatialNeighborCount;
            var["gGatherRadius"] = mSpatialReuseRadius;
            var["gSpatialRoundId"] = spatialRoundId;
            var["gSmallWindowRadius"] = mSmallWindowRestirWindowRadius;
            var["gFeatureBasedRejection"] = mFeatureBasedRejection;
            var["neighborOffsets"] = mpNeighborOffsets;
        }

        if (mOutputNRDData && !isPathReuseMISWeightComputation)
        {
            var["outputNRDDiffuseRadianceHitDist"] = renderData[kOutputNRDDiffuseRadianceHitDist]->asTexture();
            var["outputNRDSpecularRadianceHitDist"] = renderData[kOutputNRDSpecularRadianceHitDist]->asTexture();
            var["outputNRDResidualRadianceHitDist"] = renderData[kOutputNRDResidualRadianceHitDist]->asTexture();
            var["primaryHitEmission"] = renderData[kOutputNRDEmission]->asTexture();
            var["gSppId"] = restir_i;
        }
    }


    if (!isPathReuseMISWeightComputation)
    {
        var["directLighting"] = renderData[kInputDirectLighting]->asTexture();
        var["useDirectLighting"] = mUseDirectLighting;
    }
    var["gIsLastRound"] = mStaticParams.pathSamplingMode == PathSamplingMode::PathReuse || isLastRound;

    pass["gScene"] = mpScene->getParameterBlock();
    pass["gPathTracer"] = mpPathTracerBlock;

    mpPixelStats->prepareProgram(pass->getProgram(), pass->getRootVar());
    mpPixelDebug->prepareProgram(pass->getProgram(), pass->getRootVar());

    {
        // Launch one thread per pixel.
        // The dimensions are padded to whole tiles to allow re-indexing the threads in the shader.
        pass->execute(pRenderContext, { mParams.screenTiles.x * kScreenTileDim.x, mParams.screenTiles.y * kScreenTileDim.y, 1u });
    }
}

void ReSTIRPTPass::PathRetracePass(RenderContext* pRenderContext, uint32_t restir_i, const RenderData& renderData, bool temporalReuse /*= false*/, int spatialRoundId /*= 0*/)
{
    PROFILE(temporalReuse ? "temporalPathRetrace" : "spatialPathRetrace");
    ComputePass::SharedPtr pass = (temporalReuse ? mpTemporalPathRetracePass : mpSpatialPathRetracePass);

    // Check shader assumptions.
    // We launch one thread group per screen tile, with threads linearly indexed.
    const uint32_t tileSize = kScreenTileDim.x * kScreenTileDim.y;
    assert(kScreenTileDim.x == 16 && kScreenTileDim.y == 16); // TODO: Remove this temporary limitation when Slang bug has been fixed, see comments in shader.
    assert(kScreenTileBits.x <= 4 && kScreenTileBits.y <= 4); // Since we use 8-bit deinterleave.
    assert(pass->getThreadGroupSize().x == 16);
    assert(pass->getThreadGroupSize().y == 16 && pass->getThreadGroupSize().z == 1);

    // Additional specialization. This shouldn't change resource declarations.
    pass->addDefine("OUTPUT_TIME", mOutputTime ? "1" : "0");
    pass->addDefine("TEMPORAL_REUSE", temporalReuse ? "1" : "0");

    // Bind resources.
    auto var = pass->getRootVar()["CB"]["gPathRetracePass"];

    // TODO: refactor arguments
    setShaderData(var, renderData, false, false);
    var["outputReservoirs"] = spatialRoundId % 2 == 1 ? mpTemporalReservoirs[restir_i] : mpOutputReservoirs;

    if (mStaticParams.pathSamplingMode == PathSamplingMode::PathReuse)
    {
        var["nRooksPattern"] = mNRooksPatternBuffer;
    }

    var["temporalReservoirs"] = spatialRoundId % 2 == 0 ? mpTemporalReservoirs[restir_i] : mpOutputReservoirs;
    var["reconnectionDataBuffer"] = mReconnectionDataBuffer;
    var["gNumSpatialRounds"] = mNumSpatialRounds;

    if (temporalReuse)
    {
        var["temporalVbuffer"] = mpTemporalVBuffer;
        var["motionVectors"] = renderData[kInputMotionVectors]->asTexture();
        var["gEnableTemporalReprojection"] = mEnableTemporalReprojection;
        var["gNoResamplingForTemporalReuse"] = mNoResamplingForTemporalReuse;
        if (!mUseMaxHistory) var["gTemporalHistoryLength"] = 1e30f;
        else var["gTemporalHistoryLength"] = (float)mTemporalHistoryLength;
    }
    else
    {
        var["gSpatialRoundId"] = spatialRoundId;
        var["neighborOffsets"] = mpNeighborOffsets;
        var["gGatherRadius"] = mSpatialReuseRadius;
        var["gNeighborCount"] = mSpatialNeighborCount;
        var["gSmallWindowRadius"] = mSmallWindowRestirWindowRadius;
        var["gSpatialReusePattern"] = mStaticParams.pathSamplingMode == PathSamplingMode::PathReuse ? (uint32_t)mPathReusePattern : (uint32_t)mSpatialReusePattern;
        var["gFeatureBasedRejection"] = mFeatureBasedRejection;
    }

    pass["gScene"] = mpScene->getParameterBlock();
    pass["gPathTracer"] = mpPathTracerBlock;

    mpPixelStats->prepareProgram(pass->getProgram(), pass->getRootVar());
    mpPixelDebug->prepareProgram(pass->getProgram(), pass->getRootVar());

    {
        // Launch one thread per pixel.
        // The dimensions are padded to whole tiles to allow re-indexing the threads in the shader.
        pass->execute(pRenderContext, { mParams.screenTiles.x * kScreenTileDim.x, mParams.screenTiles.y * kScreenTileDim.y, 1u });
    }
}

Program::DefineList ReSTIRPTPass::StaticParams::getDefines(const ReSTIRPTPass& owner) const
{
    Program::DefineList defines;

    // Path tracer configuration.
    defines.add("SAMPLES_PER_PIXEL", std::to_string(samplesPerPixel)); // 0 indicates a variable sample count
    defines.add("CANDIDATE_SAMPLES", std::to_string(candidateSamples)); // 0 indicates a variable sample count    
    defines.add("MAX_SURFACE_BOUNCES", std::to_string(maxSurfaceBounces));
    defines.add("MAX_DIFFUSE_BOUNCES", std::to_string(maxDiffuseBounces));
    defines.add("MAX_SPECULAR_BOUNCES", std::to_string(maxSpecularBounces));
    defines.add("MAX_TRANSMISSON_BOUNCES", std::to_string(maxTransmissionBounces));
    defines.add("ADJUST_SHADING_NORMALS", adjustShadingNormals ? "1" : "0");
    defines.add("USE_BSDF_SAMPLING", useBSDFSampling ? "1" : "0");
    defines.add("USE_NEE", useNEE ? "1" : "0");
    defines.add("USE_MIS", useMIS ? "1" : "0");
    defines.add("USE_RUSSIAN_ROULETTE", useRussianRoulette ? "1" : "0");
    defines.add("USE_ALPHA_TEST", useAlphaTest ? "1" : "0");
    defines.add("USE_LIGHTS_IN_DIELECTRIC_VOLUMES", useLightsInDielectricVolumes ? "1" : "0");
    defines.add("LIMIT_TRANSMISSION", limitTransmission ? "1" : "0");
    defines.add("MAX_TRANSMISSION_REFLECTION_DEPTH", std::to_string(maxTransmissionReflectionDepth));
    defines.add("MAX_TRANSMISSION_REFRACTION_DEPTH", std::to_string(maxTransmissionRefractionDepth));
    defines.add("DISABLE_CAUSTICS", disableCaustics ? "1" : "0");
    defines.add("DISABLE_DIRECT_ILLUMINATION", disableDirectIllumination ? "1" : "0");
    defines.add("PRIMARY_LOD_MODE", std::to_string((uint32_t)primaryLodMode));
    defines.add("USE_NRD_DEMODULATION", useNRDDemodulation ? "1" : "0");
    defines.add("COLOR_FORMAT", std::to_string((uint32_t)colorFormat));
    defines.add("MIS_HEURISTIC", std::to_string((uint32_t)misHeuristic));
    defines.add("MIS_POWER_EXPONENT", std::to_string(misPowerExponent));
    defines.add("_USE_DETERMINISTIC_BSDF", useDeterministicBSDF ? "1" : "0");
    defines.add("NEIGHBOR_OFFSET_COUNT", std::to_string(kNeighborOffsetCount));
    defines.add("SHIFT_STRATEGY", std::to_string((uint32_t)shiftStrategy));
    defines.add("PATH_SAMPLING_MODE", std::to_string((uint32_t)pathSamplingMode));

    // Sampling utilities configuration.
    assert(owner.mpSampleGenerator);
    defines.add(owner.mpSampleGenerator->getDefines());

    // We don't use the legacy shading code anymore (MaterialShading.slang).
    defines.add("_USE_LEGACY_SHADING_CODE", "0");

    defines.add("INTERIOR_LIST_SLOT_COUNT", std::to_string(maxNestedMaterials));

    defines.add("GBUFFER_ADJUST_SHADING_NORMALS", owner.mGBufferAdjustShadingNormals ? "1" : "0");

    // Scene-specific configuration.
    const auto& scene = owner.mpScene;

    // Set default (off) values for additional features.
    defines.add("OUTPUT_GUIDE_DATA", "0");
    defines.add("OUTPUT_TIME", "0");
    defines.add("OUTPUT_NRD_DATA", "0");
    defines.add("OUTPUT_NRD_ADDITIONAL_DATA", "0");

    defines.add("SPATIAL_RESTIR_MIS_KIND", std::to_string((uint32_t)spatialMisKind));
    defines.add("TEMPORAL_RESTIR_MIS_KIND", std::to_string((uint32_t)temporalMisKind));

    defines.add("TEMPORAL_UPDATE_FOR_DYNAMIC_SCENE", temporalUpdateForDynamicScene ? "1" : "0");

    defines.add("BPR", pathSamplingMode == PathSamplingMode::PathReuse ? "1" : "0");

    defines.add("SEPARATE_PATH_BSDF", separatePathBSDF ? "1" : "0");

    defines.add("RCDATA_PATH_NUM", rcDataOfflineMode ? "12" : "6");
    defines.add("RCDATA_PAD_SIZE", rcDataOfflineMode ? "2" : "1");

    return defines;
}
