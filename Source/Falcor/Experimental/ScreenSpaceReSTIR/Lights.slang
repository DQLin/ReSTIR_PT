/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/
#include "Utils/Math/MathConstants.slangh"
import Scene.Scene;
import Utils.Geometry.GeometryHelpers;
import Utils.Math.MathHelpers;
import Utils.Math.PackedFormats;
import Utils.Color.ColorHelpers;
import Utils.Sampling.AliasTable;
__exported import Utils.Sampling.SampleGeneratorInterface;
import LocalEmissiveTriangle;
__exported import LightSample;


/** Represents an evaluation of a light sample from a specific shading point.
    Incident radiance is given by geomFactor * emission.
*/
struct EvaluatedLightSample
{
    float3 dir;         ///< Direction to point sample on light.
    float distance;     ///< Distance to point sample on light.
    float geomFactor;   ///< Geometry factor (factor from solid angle measure to area measure).
    float emission;     ///< Scalar emission.
    float pdf;          ///< Pdf wrt. area.
};

struct PackedLoadedLightSample
{
    uint lightSample;
    uint type;
    uint normal;
    uint emission;
    uint3 posOrDir;
    uint pdf;
};

/** Represents a light sample that has all the data loaded for later evaluation
    from different shading points.
*/
struct LoadedLightSample
{
    enum class Type
    {
        Invalid,
        Area,
        Point,
        Distant,
    };

    LightSample lightSample;    ///< Light sample.
    Type type = Type::Invalid;  ///< Type of light sample (used during evaluation).
    float3 posOrDir;            ///< Position of or direction to light sample (depending on type).
    float3 normal;              ///< Normal of the emitting surface.
    float emission;             ///< Scalar emission.
    float pdf;                  ///< Pdf with respect to area.

    EvaluatedLightSample eval(const float3 pos)
    {
        EvaluatedLightSample result = {};

        result.emission = emission;
        result.pdf = pdf;

        switch (type)
        {
        case Type::Area:
            {
                float3 toLight = posOrDir - pos;
                float distSqr =  max(FLT_MIN, dot(toLight, toLight));

                result.distance = sqrt(distSqr);
                result.dir = toLight / result.distance;
                result.geomFactor = max(0.f, dot(normal, -result.dir)) / distSqr;
            }
            break;
        case Type::Point:
            {
                float3 toLight = posOrDir - pos;
                float distSqr =  max(FLT_MIN, dot(toLight, toLight));

                result.distance = sqrt(distSqr);
                result.dir = toLight / result.distance;
                result.geomFactor = 1.f / distSqr;
            }
            break;
        case Type::Distant:
            {
                result.distance = Lights::kRayTMax;
                result.dir = posOrDir;
                result.geomFactor = 1.f;
            }
            break;
        }

        return result;
    }

    static LoadedLightSample createFromPacked(const PackedLoadedLightSample packed)
    {
        LoadedLightSample lls;
        lls.unpack(packed);
        return lls;
    }

    PackedLoadedLightSample pack()
    {
        PackedLoadedLightSample packed;
        packed.lightSample = lightSample.pack();
        packed.type = uint(type);
        packed.normal = encodeNormal2x16(normal);
        packed.emission = asuint(emission);
        packed.posOrDir = asuint(posOrDir);
        packed.pdf = asuint(pdf);
        return packed;
    }

    [mutating] void unpack(const PackedLoadedLightSample packed)
    {
        lightSample.unpack(packed.lightSample);
        type = LoadedLightSample::Type(packed.type);
        normal = decodeNormal2x16(packed.normal);
        emission = asfloat(packed.emission);
        posOrDir = asfloat(packed.posOrDir);
        pdf = asfloat(packed.pdf);
    }
};


/** Contains data structures for sampling and evaluating lights in the scene.
*/
struct Lights
{
    // Static configuration.
    static const bool kUseEnvLight = USE_ENV_LIGHT;
    static const bool kUseEmissiveLights = USE_EMISSIVE_LIGHTS;
    static const bool kUseAnalyticLights = USE_ANALYTIC_LIGHTS;

    static const bool kUseEmissiveTextureForSampling = USE_EMISSIVE_TEXTURE_FOR_SAMPLING;   ///< Use emissive texture for light sample evaluation (evalLightSample).
    static const bool kUseEmissiveTextureForShading = USE_EMISSIVE_TEXTURE_FOR_SHADING;     ///< Use emissive texture for shading (evalEmission).
    static const bool kUseLocalEmissiveTriangles = USE_LOCAL_EMISSIVE_TRIANGLES;            ///< Use local emissive triangle data structure (for more efficient sampling/evaluation).

    static const float kRayTMax = FLT_MAX;

    // If we are using the compact local emissive triangles, we need to use a ray epsilon to
    // avoid self-intersections when querying shadow rays because the computed positions don't
    // accuratly match the geometry in the scene.
    static const float kRayEpsilon = kUseLocalEmissiveTriangles ? 0.001f : 0.f;

    // Resources.
    Buffer<float> envLightLuminance;            ///< Luminance values of the env map.
    StructuredBuffer<PackedLocalEmissiveTriangle> emissiveTriangles; ///< Packed emissive triangles.

    AliasTable envLightAliasTable;              ///< Alias table for sampling the env light.
    AliasTable emissiveLightAliasTable;         ///< Alias table for sampling the emissive lights.
    AliasTable analyticLightAliasTable;         ///< Alias table for sampling the analytic lights.

    float envLightLuminanceFactor;              ///< Luminance factor to apply to env map (derived from env map intensity/tint).

    float envLightSelectionProbability;         ///< Probability for sampling the env light.
    float emissiveLightSelectionProbability;    ///< Probability for sampling an emissive light.
    float analyticLightSelectionProbability;    ///< Probability for sampling an analytic light.

    /** Sample the env map.
        \param[in] random Random number for selecting light (stratified).
        \param[in,out] sg Sample generator.
        \return Returns a new light sample.
    */
    LightSample sampleEnv<S : ISampleGenerator>(float random, inout S sg)
    {
        return LightSample::createEnv(envLightAliasTable.sample(float2(random, sampleNext1D(sg))), sg.next());
    }

    /** Sample the emissive lights.
        \param[in] random Random number for selecting light (stratified).
        \param[in,out] sg Sample generator.
        \return Returns a new light sample.
    */
    LightSample sampleEmissive<S : ISampleGenerator>(float random, inout S sg)
    {
        return LightSample::createEmissive(emissiveLightAliasTable.sample(float2(random, sampleNext1D(sg))), sg.next());
    }

    /** Sample the analytic lights.
        \param[in] random Random number for selecting light (stratified).
        \param[in,out] sg Sample generator.
        \return Returns a new light sample.
    */
    LightSample sampleAnalytic<S : ISampleGenerator>(float random, inout S sg)
    {
        return LightSample::createAnalytic(analyticLightAliasTable.sample(float2(random, sampleNext1D(sg))), sg.next());
    }

    /** Load a light sample.
        \param[in] lightSample Light sample.
        \return Returns the loaded light sample.
    */
    LoadedLightSample loadLightSample(const LightSample lightSample)
    {
        if (lightSample.isValid())
        {
            switch (lightSample.getKind())
            {
            case LightKind::Env:
                return loadEnvLightSample(lightSample);
            case LightKind::Emissive:
                return loadEmissiveLightSample(lightSample);
            case LightKind::Analytic:
                return loadAnalyticLightSample(lightSample);
            }
        }
        return { lightSample };
    }

    /** Load a packed light sample.
        \param[in] packed Packed light sample.
        \return Returns the loaded light sample.
    */
    LoadedLightSample loadFromPacked(const PackedLightSample packed)
    {
        return loadLightSample(LightSample::createFromPacked(packed));
    }

    /** Load an environment light sample.
        Requires the light sample to be of kind LightKind::Env.
        \param[in] Light sample.
        \return Returns the loaded light sample.
    */
    LoadedLightSample loadEnvLightSample(const LightSample lightSample)
    {
        LoadedLightSample result = { lightSample, LoadedLightSample::Type::Distant };

        result.posOrDir = lightSample.getEnvWorldDir();
        float luminance = envLightLuminance[lightSample.getEnvIndex()];
        result.emission = luminance * envLightLuminanceFactor;
        result.pdf = envLightSelectionProbability * luminance / envLightAliasTable.weightSum;

        return result;
    }

    /** Load an emissive light sample.
        Requires the light sample to be of kind LightKind::Emissive.
        \param[in] Light sample.
        \return Returns the loaded light sample.
    */
    LoadedLightSample loadEmissiveLightSample(const LightSample lightSample)
    {
        LoadedLightSample result = { lightSample, LoadedLightSample::Type::Area };

        if (kUseLocalEmissiveTriangles)
        {
            const LocalEmissiveTriangle tri = LocalEmissiveTriangle::createFromPacked(emissiveTriangles[lightSample.getEmissiveIndex()]);
            // If we are using the compact local emissive triangles, we have to use a ray epsilon to avoid self-intersections
            // because the positions/edges do not fully match the actual geometry in the scene. Therefore we don't care
            // for offsetting the position on the emissive triangle.
            result.posOrDir = tri.getPosition(lightSample.getEmissiveUV());
            result.normal = tri.normal;
            result.emission = tri.radiance;
            result.pdf = emissiveLightSelectionProbability * tri.radiance / emissiveLightAliasTable.weightSum;

            if (kUseEmissiveTextureForSampling)
            {
                const EmissiveTriangle tri = gScene.lightCollection.getTriangle(lightSample.getEmissiveIndex());
                float2 texCoord = tri.getTexCoord(lightSample.getEmissiveBarycentrics());
                result.emission = luminance(gScene.getEmissive(tri.materialID, texCoord, gScene.materialResources[tri.materialID]));
            }
        }
        else
        {
            const EmissiveTriangle tri = gScene.lightCollection.getTriangle(lightSample.getEmissiveIndex());
            // If we are using the scene's emissive triangles, the emissive triangle positions match the actual
            // geometry and we can offset the position on the emissive triangle to avoid self-intersections
            // when tracing shadow rays.
            result.posOrDir = computeRayOrigin(tri.getPosition(lightSample.getEmissiveBarycentrics()), tri.normal);
            result.normal = tri.normal;
            float radiance = luminance(gScene.lightCollection.getAverageRadiance(lightSample.getEmissiveIndex()));
            result.emission = radiance;
            result.pdf = emissiveLightSelectionProbability * radiance / emissiveLightAliasTable.weightSum;

            if (kUseEmissiveTextureForSampling)
            {
                float2 texCoord = tri.getTexCoord(lightSample.getEmissiveBarycentrics());
                result.emission = luminance(gScene.getEmissive(tri.materialID, texCoord, gScene.materialResources[tri.materialID]));
            }
        }

        return result;
    }

    /** Load an analytic light sample.
        Requires the light sample to be of kind LightKind::Analytic.
        \param[in] Light sample.
        \return Returns the loaded light sample.
    */
    LoadedLightSample loadAnalyticLightSample(const LightSample lightSample)
    {
        LoadedLightSample result = { lightSample };

        LightData light = gScene.getLight(lightSample.getAnalyticIndex());
        LightType type = LightType(light.type);
        if (type == LightType::Point)
        {
            result.type = LoadedLightSample::Type::Point;
            result.posOrDir = light.posW;
            result.emission = luminance(light.intensity);
            result.pdf = analyticLightSelectionProbability / analyticLightAliasTable.weightSum;
        }
        else if (type == LightType::Directional)
        {
            result.type = LoadedLightSample::Type::Distant;
            result.posOrDir = -light.dirW;
            result.emission = luminance(light.intensity);
            result.pdf = analyticLightSelectionProbability / analyticLightAliasTable.weightSum;
        }
        else if (type == LightType::Distant)
        {
            result.type = LoadedLightSample::Type::Distant;
            float3 dir = sample_cone(lightSample.getAnalyticPosition(), light.cosSubtendedAngle);
            result.posOrDir = normalize(mul(dir, (float3x3)light.transMat));
            result.emission = luminance(light.intensity);
            result.pdf = analyticLightSelectionProbability / analyticLightAliasTable.weightSum;
        }
        else
        {
            // TODO: Implement other light types.
            result.pdf = 1.f; // Avoid division by zero
        }

        return result;
    }

    /** Evaluates a light sample from the given shading position.
        \param[in] lightSample Light sample to evaluate.
        \param[in] pos Shading position.
        \return Returns the evaluated light sample.
    */
    EvaluatedLightSample evalLightSample(const LightSample lightSample, const float3 pos)
    {
        return loadLightSample(lightSample).eval(pos);
    }

    /** Evaluate the spectral emission of a light sample.
        \param lightSample Light sample.
        \return Returns the spectral emission.
    */
    float3 evalEmission(const LightSample lightSample)
    {
        if (lightSample.isValid())
        {
            switch (lightSample.getKind())
            {
            case LightKind::Env:
                return evalEnvEmission(lightSample);
            case LightKind::Emissive:
                return evalEmissiveEmission(lightSample);
            case LightKind::Analytic:
                return evalAnalyticEmission(lightSample);
            }
        }
        return float3(0.f);
    }

    /** Evaluate the spectral emission of a light sample.
        Requires the light sample to be of kind LightKind::Env.
        \param lightSample Light sample.
        \return Returns the spectral emission.
    */
    float3 evalEnvEmission(const LightSample lightSample)
    {
        float3 dir = lightSample.getEnvWorldDir();
        return gScene.envMap.eval(dir);
    }

    /** Evaluate the spectral emission of a light sample.
        Requires the light sample to be of kind LightKind::Emissive.
        \param lightSample Light sample.
        \return Returns the spectral emission.
    */
    float3 evalEmissiveEmission(const LightSample lightSample)
    {
        uint emissiveIndex = lightSample.getEmissiveIndex();
        const EmissiveTriangle tri = gScene.lightCollection.getTriangle(emissiveIndex);
        if (kUseEmissiveTextureForShading)
        {
            float2 texCoord = tri.getTexCoord(lightSample.getEmissiveBarycentrics());
            return gScene.getEmissive(tri.materialID, texCoord, gScene.materialResources[tri.materialID]);
        }
        else
        {
            return gScene.lightCollection.getAverageRadiance(emissiveIndex);
        }
    }

    /** Evaluate the spectral emission of a light sample.
        Requires the light sample to be of kind LightKind::Analytic.
        \param lightSample Light sample.
        \return Returns the spectral emission.
    */
    float3 evalAnalyticEmission(const LightSample lightSample)
    {
        LightData light = gScene.getLight(lightSample.getAnalyticIndex());
        LightType type = LightType(light.type);
        switch (LightType(light.type))
        {
        case LightType::Point:
        case LightType::Directional:
        case LightType::Distant:
            return light.intensity;
        default:
            // TODO: Implement other light types.
            return float3(0.f);
        }
    }
};
