/***************************************************************************
 # Copyright (c) 2022, Daqi Lin.  All rights reserved.
 **************************************************************************/
__exported import Rendering.Materials.TexLODTypes;

/** Translation of defines set by the host to compile-time constants used to
    configure the path tracer without overhead from dynamic control flow.
    This will eventually be replaced by specialization constants in Slang.
*/

#ifndef SAMPLES_PER_PIXEL
#error StaticParams.slang is missing defines
#endif

static const uint kSamplesPerPixel = SAMPLES_PER_PIXEL;
static const uint kCandidateSamples = CANDIDATE_SAMPLES;
static const uint kMaxSurfaceBounces = MAX_SURFACE_BOUNCES;
static const uint kMaxDiffuseBounces = MAX_DIFFUSE_BOUNCES;
static const uint kMaxSpecularBounces = MAX_SPECULAR_BOUNCES;
static const uint kMaxTransmissionBounces = MAX_TRANSMISSON_BOUNCES;
static const bool kAdjustShadingNormals = ADJUST_SHADING_NORMALS;
static const bool kUseBSDFSampling = USE_BSDF_SAMPLING;
static const bool kUseNEE = USE_NEE;
static const bool kUseMIS = USE_MIS;
static const bool kUseRussianRoulette = USE_RUSSIAN_ROULETTE;
static const bool kUseAlphaTest = USE_ALPHA_TEST;
static const bool kUseLightsInDielectricVolumes = USE_LIGHTS_IN_DIELECTRIC_VOLUMES;
static const bool kLimitTransmission = LIMIT_TRANSMISSION;
static const uint kMaxTransmissionReflectionDepth = MAX_TRANSMISSION_REFLECTION_DEPTH;
static const uint kMaxTransmissionRefractionDepth = MAX_TRANSMISSION_REFRACTION_DEPTH;
static const bool kDisableCaustics = DISABLE_CAUSTICS;
static const bool kDisableDirectIllumination = DISABLE_DIRECT_ILLUMINATION;
static const TexLODMode kPrimaryLodMode = TexLODMode(PRIMARY_LOD_MODE);
static const uint kColorFormat = COLOR_FORMAT;
static const uint kMISHeuristic = MIS_HEURISTIC;
static const float kMISPowerExponent = MIS_POWER_EXPONENT;
static const uint kShiftStrategy = SHIFT_STRATEGY;
static const uint kSpatialReSTIRMISKind = SPATIAL_RESTIR_MIS_KIND;
static const uint kTemporalReSTIRMISKind = TEMPORAL_RESTIR_MIS_KIND;
static const bool kTemporalUpdateForDynamicScene = TEMPORAL_UPDATE_FOR_DYNAMIC_SCENE;
static const int kMaximumPathLength = 15;
static const uint kPathSamplingMode = PATH_SAMPLING_MODE;
static const bool kSeparatePathBSDF = SEPARATE_PATH_BSDF;

static const bool kUseNRDDemodulation = USE_NRD_DEMODULATION;
