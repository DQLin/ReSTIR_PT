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
#include "Utils/Math/MathConstants.slangh"

import Utils.Geometry.IntersectionHelpers;
import Scene.SDFs.SDFVoxelCommon;

struct NDSDFGrid
{
    static const float kMinStepSize = 0.001f;
    static const uint kMaxSteps = 512;

#if defined(SCENE_SDF_GRID_MAX_LOD_COUNT) && SCENE_SDF_GRID_MAX_LOD_COUNT > 0
    Texture3D<float> textures[SCENE_SDF_GRID_MAX_LOD_COUNT];
#else
    Texture3D<float> textures[1];
#endif
    SamplerState sampler;
    uint lodCount;
    uint coarsestLODAsLevel;
    uint coarsestLODGridWidth;
    float coarsestLODNormalizationFactor;
    float narrowBandThickness;

    // Calculates a new finer LOD from the normalized distance in the SDF grid.
    // If abs(d) lies in [0.5, 1.0) the LOD will be increased by 1.
    // If abs(d) lies in [0.25, 0.5) the LOD will be increased by 2.
    // If abs(d) lies in [0.125, 0.25) the LOD will be increased by 3.
    // ... and so on.
    int calculateDeltaFinerLOD(const float d)
    {
        float invAbsD = 1.0f / abs(d);
        return int(floor(log2(invAbsD))) + 1;
    }

    /** Samples the SDF grid using HW to perform trilinear interpolation, less accurate.
    */
    float hwSample(const float3 p, const uint lod)
    {
        float lodGridWidth = float(coarsestLODGridWidth << lod);
        float3 voxelUnitCoords = (p * lodGridWidth + 0.5f) / (lodGridWidth + 1.0f);

        return textures[lod].SampleLevel(sampler, voxelUnitCoords, 0);
    }

    void calculateVoxelCoords(const float3 p, const uint lod, out int3 voxelCoords, out float3 voxelUnitCoords)
    {
        // Clamp so we don't risk loading values outside the texture.
        float3 pClamped = clamp(p, 0.0f, 1.0f);

        float lodGridWidth = float(coarsestLODGridWidth << lod);
        float3 voxelPosition = float3(pClamped * lodGridWidth);

        voxelCoords = int3(voxelPosition);
        voxelUnitCoords = frac(voxelPosition);
    }

    /** Calculates the grid width at a give LOD.
    */
    uint calculateGridWidth(const uint lod)
    {
        return coarsestLODGridWidth << lod;
    }

    /** Loads the corner values of a voxel.
    */
    void loadCornerValues(const int3 voxelCoords, const uint lod, out float4 values0xx, out float4 values1xx)
    {
        Texture3D<float> currentLODGrid = textures[lod];
        values0xx[0] = currentLODGrid.Load(int4(voxelCoords, 0));
        values0xx[1] = currentLODGrid.Load(int4(voxelCoords, 0), int3(0, 0, 1));
        values0xx[2] = currentLODGrid.Load(int4(voxelCoords, 0), int3(0, 1, 0));
        values0xx[3] = currentLODGrid.Load(int4(voxelCoords, 0), int3(0, 1, 1));
        values1xx[0] = currentLODGrid.Load(int4(voxelCoords, 0), int3(1, 0, 0));
        values1xx[1] = currentLODGrid.Load(int4(voxelCoords, 0), int3(1, 0, 1));
        values1xx[2] = currentLODGrid.Load(int4(voxelCoords, 0), int3(1, 1, 0));
        values1xx[3] = currentLODGrid.Load(int4(voxelCoords, 0), int3(1, 1, 1));
    }

    /** Converts the lod into a level, the lod is relative to the coarsest grid width, while the level is always relative to a 1^3 grid.
    */
    uint lodToLevel(const uint lod)
    {
        return coarsestLODAsLevel + lod;
    }

    /** Converts the lod into a level, the lod is relative to the coarsest grid width, while the level is always relative to a 1^3 grid.
    */
    uint levelToLOD(const uint level)
    {
        return level - min(coarsestLODAsLevel, level);
    }

    /** Samples the SDF grid using SW to perform trilinear interpolation, more accurate.
    */
    float swSample(const float3 p, const uint lod)
    {
        int3 voxelCoords;
        float3 voxelUnitCoords;
        calculateVoxelCoords(p, lod, voxelCoords, voxelUnitCoords);

        float4 values0xx;
        float4 values1xx;
        loadCornerValues(voxelCoords, lod, values0xx, values1xx);

        return SDFVoxelCommon::sdfVoxelTrilin(values0xx, values1xx, voxelUnitCoords);
    }

    /** Intersect a ray with the normalized dense SDF grid. The ray must be transformed to the local space of the SDF grid prior to calling this.
        \param[in] rayOrigin The origin of the ray in the local space of the SDF grid.
        \param[in] rayDir The direction of the ray in the local space of the SDF grid, note that this should not be normalized if the SDF grid has been scaled.
        \param[in] tMin Minimum valid value for t.
        \param[in] tMax Maximum valid value for t.
        \param[in] solverMaxStepCount If using a numeric voxel intersection method, this is the maximum number of steps the method can use.
        \param[out] t Intersection t.
        \param[out] hitData The LOD of the intersection.
        \return True if the ray intersects the normalized dense SDF grid, false otherwise.
    */
    bool intersectSDF(const float3 rayOrigin, const float3 rayDir, const float tMin, const float tMax, const uint solverMaxStepCount, out float t, out uint hitData)
    {
        // Add 0.5f to origin so that it is in [0, 1] instead of [-0.5, 0.5].
        float3 rayOrigLocal = rayOrigin + 0.5f;

        // Normalize ray direction.
        float dirLength = length(rayDir);
        float inverseDirLength = 1.0f / dirLength;
        float3 rayDirLocal = rayDir * inverseDirLength;

#if SCENE_SDF_VOXEL_INTERSECTION_METHOD != SCENE_SDF_NO_VOXEL_SOLVER
        // Clamp direction to epsilon to avoid division by zero.
        float3 d = rayDirLocal;
        d.x = abs(d.x) < FLT_EPSILON ? FLT_EPSILON * sign(d.x) : d.x;
        d.y = abs(d.y) < FLT_EPSILON ? FLT_EPSILON * sign(d.y) : d.y;
        d.z = abs(d.z) < FLT_EPSILON ? FLT_EPSILON * sign(d.z) : d.z;
#endif

        // Find near and far plane.
        float2 nearFar;
        if (!intersectRayAABB(rayOrigLocal, rayDirLocal, float3(0.0f), float3(1.0f), nearFar))
            return false;

        // Set up t and tMax.
        t = max(tMin * dirLength, nearFar.x);
        float tMaxLocal = min(tMax * dirLength, nearFar.y);

        // Check that the ray segment overlaps the AABB.
        if (tMaxLocal < t) return false;

        // Set up LOD variables
        const uint maxLOD = lodCount - 1;
        int lod = maxLOD;
        float normalizationFactor = coarsestLODNormalizationFactor / float(1 << lod);

        // Set up step variables
        float3 pLocal = rayOrigLocal + t * rayDirLocal;
        float currD = swSample(pLocal, lod);

        float lastD = 1.0f;
        float clampedD = max(currD, kMinStepSize);

        // Check if we're already inside the surface
        if (currD <= 0.0f)
        {
            t = tMin;

            hitData = lod;
            return true;
        }

        float prevT;
        float currH = clampedD * normalizationFactor;
        float nextH;

        uint steps = 0;
        for (; steps < kMaxSteps; steps++)
        {
            // Update t.
            prevT = t;
            t += currH;

            // Check if we're outside farplane.
            if (t > tMaxLocal)
            {
                return false;
            }

            // Update position.
            pLocal = rayOrigLocal + t * rayDirLocal;

            // Probe the current location at the current LOD, use the result to calculate a new LOD.
            currD = hwSample(pLocal, lod);

            if (abs(currD) >= 1.0f)
            {
                // Move to coarser LOD and update variables.
                lod = max(lod - 1, 0);
                normalizationFactor = coarsestLODNormalizationFactor / float(1 << lod);

                lastD = currD;

                // We're "far" away from the surface, use inaccurate HW interpolation.
                currD = hwSample(pLocal, lod);

#if SCENE_SDF_VOXEL_INTERSECTION_METHOD != SCENE_SDF_NO_VOXEL_SOLVER
                // Check if we're inside the surface.
                if (currD <= 0.0f)
                {
                    // If we are, and at the maximum lod, skip clamping to minstep, denormalize, and break.
                    if (lod == maxLOD)
                    {
                        nextH = currD * normalizationFactor;
                        break;
                    }
                    else
                    {
                        // Jump up to finer LOD that has data.
                        int deltaLOD = calculateDeltaFinerLOD(max(currD, 0.0f));

                        // Move to finer LOD and update variables.
                        lod = min(lod + deltaLOD, maxLOD);
                        normalizationFactor = coarsestLODNormalizationFactor / float(1 << lod);

                        // Retrace last step at lower LOD.
                        t = prevT;
                    }
                }
#endif
            }
            else
            {
                if (currD < 1.0f)
                {
                    // Jump up to finer LOD that has data.
                    int deltaLOD = calculateDeltaFinerLOD(currD);

                    // Move to finer LOD and update variables.
                    lod = min(lod + deltaLOD, maxLOD);
                    normalizationFactor = coarsestLODNormalizationFactor / float(1 << lod);
                }

                lastD = currD;

                // We are potentially close to the surface, use more accurate SW interpolation.
                int3 voxelCoords;
                float3 voxelUnitCoords;
                calculateVoxelCoords(pLocal, lod, voxelCoords, voxelUnitCoords);

                float4 values0xx;
                float4 values1xx;
                loadCornerValues(voxelCoords, lod, values0xx, values1xx);

                currD = SDFVoxelCommon::sdfVoxelTrilin(values0xx, values1xx, voxelUnitCoords);

#if SCENE_SDF_VOXEL_INTERSECTION_METHOD != SCENE_SDF_NO_VOXEL_SOLVER
                // Check if we're inside the surface.
                if (currD <= 0.0f)
                {
                    // If we are, and at the maximum lod, skip clamping to minstep, denormalize, and break.
                    if (lod == maxLOD)
                    {
                        nextH = currD * normalizationFactor;
                        break;
                    }
                    else
                    {
                        // Jump up to finer LOD that has data.
                        int deltaLOD = calculateDeltaFinerLOD(max(currD, 0.0f));

                        // Move to finer LOD and update variables.
                        lod = min(lod + deltaLOD, maxLOD);
                        normalizationFactor = coarsestLODNormalizationFactor / float(1 << lod);

                        // Retrace last step at lower LOD.
                        t = prevT;
                    }
                }
                else if (SDFVoxelCommon::containsSurface(values0xx, values1xx))
                {
                    if (lod == maxLOD)
                    {
                        float lodGridWidth = float(coarsestLODGridWidth << lod);

                        float3 tLocalMaximums = (step(float3(0.0f), d) - voxelUnitCoords) / d;
                        float tLocalMax = min(min(min((tMaxLocal - t) * lodGridWidth, tLocalMaximums.x), tLocalMaximums.y), tLocalMaximums.z);

                        // Divide by narrow band thickness so that a 1 represents one voxel diagonal.
                        values0xx /= narrowBandThickness;
                        values1xx /= narrowBandThickness;

                        float tLocal;
                        if (SDFVoxelCommon::intersectSDFVoxel(voxelUnitCoords, d, false, values0xx, values1xx, tLocalMax, solverMaxStepCount, tLocal))
                        {
                            t += tLocal / lodGridWidth;
                            nextH = 0.0f;
                            break;
                        }

                        // No hit in voxel, move on to next voxel.
                        float voxelBorderDistance = tLocalMax / lodGridWidth;
                        float sampledSurfaceDistance = currD * normalizationFactor;
                        currH = max(voxelBorderDistance, sampledSurfaceDistance);
                        continue;
                    }
                    else
                    {
                        // Jump up to finer LOD that has data.
                        int deltaLOD = calculateDeltaFinerLOD(max(currD, 0.0f));

                        // Move to finer LOD and update variables.
                        lod = min(lod + deltaLOD, maxLOD);
                        normalizationFactor = coarsestLODNormalizationFactor / float(1 << lod);

                        // Retrace last step at lower LOD.
                        t = prevT;
                    }
                }
#endif
            }

#if SCENE_SDF_VOXEL_INTERSECTION_METHOD != SCENE_SDF_NO_VOXEL_SOLVER
            currH = currD * normalizationFactor;
#else
            // Check if we're inside the surface.
            if (currD <= 0.0f)
            {
                // If we are, and at the maximum lod, skip clamping to minstep, denormalize, and break.
                if (lod == maxLOD)
                {
                    nextH = currD * normalizationFactor;
                    break;
                }
                else
                {
                    // Jump up to finer LOD that has data.
                    int deltaLOD = calculateDeltaFinerLOD(max(currD, 0.0f));

                    // Move to finer LOD and update variables.
                    lod = min(lod + deltaLOD, maxLOD);
                    normalizationFactor = coarsestLODNormalizationFactor / float(1 << lod);

                    // Retrace last step at lower LOD.
                    t = prevT;
                }
            }

            // If we are outside the surface, clamp to minstep, denormalize and update currH.
            clampedD = max(currD, kMinStepSize);
            currH = clampedD * normalizationFactor;
#endif
        }

        // Linear interpolation to approximate intersection point.
        float finalH = currH * nextH / (currH - nextH);
        t += finalH;
        hitData = lod;
        t *= inverseDirLength;

        return steps < kMaxSteps;
    }

    /** Intersect a ray with the normalized dense SDF grid, does not return information about the intersection. The ray must be transformed to the local space of the SDF grid prior to calling this.
        \param[in] rayOrigin The origin of the ray in the local space of the SDF grid.
        \param[in] rayDir The direction of the ray in the local space of the SDF grid, note that this should not be normalized if the SDF grid has been scaled.
        \param[in] coarsestLODNormalizationFactor The normalization factor that was used to normalize the corner distances at the coarsest LOD.
        \param[in] tMin Minimum valid value for t.
        \param[in] tMax Maximum valid value for t.
        \param[in] solverMaxStepCount If using a numeric voxel intersection method, this is the maximum number of steps the method can use.
        \return True if the ray intersects the normalized dense SDF grid, false otherwise.
    */
    bool intersectSDFAny(const float3 rayOrigin, const float3 rayDir, const float tMin, const float tMax, const uint solverMaxStepCount)
    {
        // Add 0.5f to origin so that it is in [0, 1] instead of [-0.5, 0.5].
        float3 rayOrigLocal = rayOrigin + 0.5f;

        // Normalize ray direction.
        float dirLength = length(rayDir);
        float inverseDirLength = 1.0f / dirLength;
        float3 rayDirLocal = rayDir * inverseDirLength;

#if SCENE_SDF_VOXEL_INTERSECTION_METHOD != SCENE_SDF_NO_VOXEL_SOLVER
        // Clamp direction to epsilon to avoid division by zero.
        float3 d = rayDirLocal;
        d.x = abs(d.x) < FLT_EPSILON ? FLT_EPSILON * sign(d.x) : d.x;
        d.y = abs(d.y) < FLT_EPSILON ? FLT_EPSILON * sign(d.y) : d.y;
        d.z = abs(d.z) < FLT_EPSILON ? FLT_EPSILON * sign(d.z) : d.z;
#endif

        // Find near and far plane.
        float2 nearFar;
        if (!intersectRayAABB(rayOrigLocal, rayDirLocal, float3(0.0f), float3(1.0f), nearFar))
            return false;

        // Set up t and tMax.
        float t = max(tMin * dirLength, nearFar.x);
        float tMaxLocal = min(tMax * dirLength, nearFar.y);

        // Check that the ray segment overlaps the AABB.
        if (tMaxLocal < t) return false;

        // Set up LOD variables
        const uint maxLOD = lodCount - 1;
        int lod = maxLOD;
        float normalizationFactor = coarsestLODNormalizationFactor / float(1 << lod);

        // Set up step variables
        float3 pLocal = rayOrigLocal + t * rayDirLocal;
        float currD = swSample(pLocal, lod);

        float lastD = 1.0f;
        float clampedD = max(currD, kMinStepSize);

        // Check if we're already inside the surface
        if (currD <= 0.0f)
        {
            return true;
        }

        float currH = clampedD * normalizationFactor;
        float nextH;

        uint steps = 0;
        for (; steps < kMaxSteps; steps++)
        {
            // Update t.
            t += currH;

            // Check if we're outside farplane.
            if (t > tMaxLocal)
            {
                return false;
            }

            // Update position.
            pLocal = rayOrigLocal + t * rayDirLocal;

            // Probe the current location at the current LOD, use the result to calculate a new LOD.
            currD = hwSample(pLocal, lod);

            if (abs(currD) >= 1.0f)
            {
                // Move to coarser LOD and update variables.
                lod = max(lod - 1, 0);
                normalizationFactor = coarsestLODNormalizationFactor / float(1 << lod);

                lastD = currD;

                // We're "far" away from the surface, use inaccurate HW interpolation.
                currD = hwSample(pLocal, lod);

#if SCENE_SDF_VOXEL_INTERSECTION_METHOD != SCENE_SDF_NO_VOXEL_SOLVER
                // Check if we're inside the surface.
                if (currD <= 0.0f)
                {
                    return true;
                }
#endif
            }
            else
            {
                if (abs(currD) < 1.0f)
                {
                    // Jump up to finest LOD that has data.
                    int deltaLOD = calculateDeltaFinerLOD(currD);

                    // Move to finer LOD and update variables.
                    lod = min(lod + deltaLOD, maxLOD);
                    normalizationFactor = coarsestLODNormalizationFactor / float(1 << lod);
                }

                lastD = currD;

                // We are potentially close to the surface, use more accurate SW interpolation.
                int3 voxelCoords;
                float3 voxelUnitCoords;
                calculateVoxelCoords(pLocal, lod, voxelCoords, voxelUnitCoords);

                float4 values0xx;
                float4 values1xx;
                loadCornerValues(voxelCoords, lod, values0xx, values1xx);

                currD = SDFVoxelCommon::sdfVoxelTrilin(values0xx, values1xx, voxelUnitCoords);

#if SCENE_SDF_VOXEL_INTERSECTION_METHOD != SCENE_SDF_NO_VOXEL_SOLVER
                // Check if we're inside the surface.
                if (currD <= 0.0f)
                {
                    return true;
                }
                else if (SDFVoxelCommon::containsSurface(values0xx, values1xx) && lod == maxLOD)
                {
                    float lodGridWidth = float(coarsestLODGridWidth << lod);

                    float3 tLocalMaximums = (step(float3(0.0f), d) - voxelUnitCoords) / d;
                    float tLocalMax = min(min(min((tMaxLocal - t) * lodGridWidth, tLocalMaximums.x), tLocalMaximums.y), tLocalMaximums.z);

                    // Divide by narrow band thickness so that a 1 represents one voxel diagonal.
                    values0xx /= narrowBandThickness;
                    values1xx /= narrowBandThickness;

                    if (SDFVoxelCommon::intersectSDFVoxelAny(voxelUnitCoords, d, false, values0xx, values1xx, tLocalMax, solverMaxStepCount))
                    {
                        return true;
                    }

                    // No hit in voxel, move on to next voxel.
                    float voxelBorderDistance = tLocalMax / lodGridWidth;
                    float sampledSurfaceDistance = currD * normalizationFactor;
                    currH = max(voxelBorderDistance, sampledSurfaceDistance);
                    continue;
                }
#endif
            }

#if SCENE_SDF_VOXEL_INTERSECTION_METHOD != SCENE_SDF_NO_VOXEL_SOLVER
            currH = currD * normalizationFactor;
#else
            // Check if we're inside the surface.
            if (currD <= 0.0f)
            {
                return true;
            }

            // If we are outside the surface, clamp to minstep, denormalize and update currH.
            clampedD = max(currD, kMinStepSize);
            currH = clampedD * normalizationFactor;
#endif
        }

        return steps < kMaxSteps;
    }

    /** Calculate the gradient of the normalized dense SDF grid at a given point. The point must be transformed to the local space of the SDF grid prior to calling this.
        \param[in] hitPosition The point where the gradient should be calculated, must be transformed to the local space of the SDF grid.
        \param[in] lod The LOD for which the gradient should be evaluated.
        \return The gradient of the SDF grid at pLocal, note that this is not guaranteed to be normalized.
    */
    float3 calculateGradient(const float3 hitPosition, const uint lod)
    {
        // Add 0.5f to hitPosition so that it is in [0, 1] instead of [-0.5, 0.5].
        float3 pLocal = hitPosition + 0.5f;

        float3 gradient;

#if SCENE_SDF_GRADIENT_EVALUATION_METHOD == SCENE_SDF_GRADIENT_NUMERIC_DISCONTINUOUS
        // Set up offset.
        const float lodGridWidth = float(coarsestLODGridWidth << lod);
        const float offset = 0.2f / lodGridWidth;

        int3 voxelCoords;
        float3 voxelUnitCoords;
        calculateVoxelCoords(pLocal, lod, voxelCoords, voxelUnitCoords);

        float4 values0xx;
        float4 values1xx;
        loadCornerValues(voxelCoords, lod, values0xx, values1xx);

        gradient = SDFVoxelCommon::computeNumericGradient(voxelUnitCoords, offset, values0xx, values1xx);
#elif SCENE_SDF_GRADIENT_EVALUATION_METHOD == SCENE_SDF_GRADIENT_NUMERIC_CONTINUOUS
        // Set up offset.
        const float lodGridWidth = float(coarsestLODGridWidth << lod);
        const float offset = 0.2f / lodGridWidth;
        float2 e = float2(1.0f, -1.0f) * offset;

        float3 xyyPos = pLocal + e.xyy;
        float3 yyxPos = pLocal + e.yyx;
        float3 yxyPos = pLocal + e.yxy;
        float3 xxxPos = pLocal + e.xxx;

        int3 xyyVoxelCoords;
        int3 yyxVoxelCoords;
        int3 yxyVoxelCoords;
        int3 xxxVoxelCoords;
        float3 xyyVoxelUnitCoords;
        float3 yyxVoxelUnitCoords;
        float3 yxyVoxelUnitCoords;
        float3 xxxVoxelUnitCoords;

        calculateVoxelCoords(xyyPos, lod, xyyVoxelCoords, xyyVoxelUnitCoords);
        calculateVoxelCoords(yyxPos, lod, yyxVoxelCoords, yyxVoxelUnitCoords);
        calculateVoxelCoords(yxyPos, lod, yxyVoxelCoords, yxyVoxelUnitCoords);
        calculateVoxelCoords(xxxPos, lod, xxxVoxelCoords, xxxVoxelUnitCoords);

        float4 xyyValues0xx;
        float4 xyyValues1xx;
        float4 yyxValues0xx;
        float4 yyxValues1xx;
        float4 yxyValues0xx;
        float4 yxyValues1xx;
        float4 xxxValues0xx;
        float4 xxxValues1xx;

        loadCornerValues(xyyVoxelCoords, lod, xyyValues0xx, xyyValues1xx);
        loadCornerValues(yyxVoxelCoords, lod, yyxValues0xx, yyxValues1xx);
        loadCornerValues(yxyVoxelCoords, lod, yxyValues0xx, yxyValues1xx);
        loadCornerValues(xxxVoxelCoords, lod, xxxValues0xx, xxxValues1xx);

        float xyyD = SDFVoxelCommon::sdfVoxelTrilin(xyyValues0xx, xyyValues1xx, xyyVoxelUnitCoords);
        float yyxD = SDFVoxelCommon::sdfVoxelTrilin(yyxValues0xx, yyxValues1xx, yyxVoxelUnitCoords);
        float yxyD = SDFVoxelCommon::sdfVoxelTrilin(yxyValues0xx, yxyValues1xx, yxyVoxelUnitCoords);
        float xxxD = SDFVoxelCommon::sdfVoxelTrilin(xxxValues0xx, xxxValues1xx, xxxVoxelUnitCoords);

        gradient =
            e.xyy * xyyD +
            e.yyx * yyxD +
            e.yxy * yxyD +
            e.xxx * xxxD;
#endif
        const float normalizationFactor = coarsestLODNormalizationFactor / float(1 << lod);
        return gradient * normalizationFactor;
    }
}
