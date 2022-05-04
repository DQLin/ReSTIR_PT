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
#if SCENE_SDF_GRID_COUNT > 0
import Scene.SDFs.SDFVoxelCommon;
import Utils.Math.FormatConversion;
import Scene.SDFs.NormalizedDenseSDFGrid.NDSDFGrid;

struct SDFGrid
{
    static const uint kSolverMaxStepCount = SCENE_SDF_SOLVER_MAX_ITERATION_COUNT;

    NDSDFGrid ndSDFGrid;

    /** Intersect a ray with the SDF grid. The ray must be transformed to the local space of the SDF grid prior to calling this.
        \param[in] rayOrigLocal The origin of the ray in the local space of the SDF grid.
        \param[in] rayDirLocal The direction of the ray in the local space of the SDF grid, note that this should not be normalized if the SDF grid has been scaled.
        \param[in] tMin Minimum valid value for t.
        \param[in] tMax Maximum valid value for t.
        \param[out] t Intersection t.
        \param[out] hitData Encodes that required to reconstruct the hit position and/or evaluate the gradient at the hit position.
        \return True if the ray intersects the SDF grid, false otherwise.
    */
    bool intersectSDF(const float3 rayOrigLocal, const float3 rayDirLocal, const float tMin, const float tMax, out float t, out uint hitData)
    {
        return ndSDFGrid.intersectSDF(rayOrigLocal, rayDirLocal, tMin, tMax, kSolverMaxStepCount, t, hitData);
    }

    /** Intersect a ray with the SDF grid, does not return information about the intersection. The ray must be transformed to the local space of the SDF grid prior to calling this.
        \param[in] rayOrigLocal The origin of the ray in the local space of the SDF grid.
        \param[in] rayDirLocal The direction of the ray in the local space of the SDF grid, note that this should not be normalized if the SDF grid has been scaled.
        \param[in] tMin Minimum valid value for t.
        \param[in] tMax Maximum valid value for t.
        \return True if the ray intersects the SDF grid, false otherwise.
    */
    bool intersectSDFAny(const float3 rayOrigLocal, const float3 rayDirLocal, const float tMin, const float tMax)
    {
        return ndSDFGrid.intersectSDFAny(rayOrigLocal, rayDirLocal, tMin, tMax, kSolverMaxStepCount);
    }

    /** Calculate the gradient of the SDF grid at a given point. The point must be transformed to the local space of the SDF grid prior to calling this.
        \param[in] pLocal The point where the gradient should be calculated, must be transformed to the local space of the SDF grid.
        \param[in] hitData Hit data returned by intersectSDF when the hit point at pLocal was found.
        \return The gradient of the SDF grid at pLocal, note that this is not guaranteed to be normalized.
    */
    float3 calculateGradient(const float3 pLocal, const uint hitData)
    {
        return ndSDFGrid.calculateGradient(pLocal, hitData);
    }
};
#else
// Create a dummy struct if no SDF grids are present in the scene.
struct SDFGrid
{
    bool intersectSDF(const float3 rayOrigLocal, const float3 rayDirLocal, const float tMin, const float tMax, out float t, out uint hitData) { return false; }

    bool intersectSDFAny(const float3 rayOrigLocal, const float3 rayDirLocal, const float tMin, const float tMax) { return false; }

    float3 calculateGradient(const float3 pLocal, const uint hitData) { return float3(0.0f); }
}
#endif
