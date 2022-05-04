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
#include "SDFGrid.h"
#include "Scene/SDFs/NormalizedDenseSDFGrid/NDSDFGrid.h"

namespace Falcor
{
    SDFGrid::SharedPtr SDFGrid::create()
    {
        // This function exists to make it possible to create the SDF grids in python.
        // In the future it will take an SDF grid type as parameter and create the correct underlying implementation.
        // TODO: Take an SDF grid type as parameter and create the corrent underlying implementation.

        return NDSDFGrid::create();
    }

    bool SDFGrid::setValues(const std::vector<float>& cornerValues, uint32_t gridWidth, float narrowBandThickness)
    {
        if (gridWidth == 0 || (gridWidth & (gridWidth - 1)) != 0)
        {
            logError("SDFGrid::setValues() gridWidth must be a power of 2");
            return false;
        }

        if (narrowBandThickness < 1.0f)
        {
            logWarning("SDFGrid::setValues() narrowBandThickness less than 1, will be clamped.");
        }

        mGridWidth = gridWidth;
        mNarrowBandThickness = narrowBandThickness;

        return setValuesInternal(cornerValues);
    }

    bool SDFGrid::loadValuesFromFile(const std::string& filename, float narrowBandThickness)
    {
        std::string filePath;
        if (findFileInDataDirectories(filename, filePath))
        {
            std::ifstream file(filePath, std::ios::in | std::ios::binary);

            if (file.is_open())
            {
                uint32_t gridWidth;
                file.read(reinterpret_cast<char*>(&gridWidth), sizeof(float));

                uint32_t totalValueCount = (gridWidth + 1) * (gridWidth + 1) * (gridWidth + 1);
                std::vector<float> cornerValues(totalValueCount, 0.0f);
                file.read(reinterpret_cast<char*>(cornerValues.data()), totalValueCount * sizeof(float));

                return setValues(cornerValues, gridWidth, narrowBandThickness);
            }
        }

        logError("SDFGrid::loadValues() file with name " + filename + " could not be opened!");
        return false;
    }

    float SDFGrid::calculateNormalizationFactor(uint32_t gridWidth)
    {
        return 0.5f * glm::root_three<float>() * mNarrowBandThickness / gridWidth;
    }

    SCRIPT_BINDING(SDFGrid)
    {
        auto createCheeseSDFGrid = [](uint32_t gridWidth, float narrowBandThickness, uint32_t seed)
        {
            SDFGrid::SharedPtr pSDFGrid = SDFGrid::create();

            const float kHalfCheeseExtent = 0.4f;
            const uint32_t kHoleCount = 32;
            float4 holes[kHoleCount];

            std::mt19937 rng(seed);
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);

            for (uint32_t s = 0; s < kHoleCount; s++)
            {
                float3 p = 2.0f * kHalfCheeseExtent * float3(dist(rng), dist(rng), dist(rng)) - float3(kHalfCheeseExtent);
                holes[s] = float4(p, dist(rng) * 0.2f + 0.01f);
            }

            uint32_t gridWidthInValues = 1 + gridWidth;
            uint32_t totalValueCount = gridWidthInValues * gridWidthInValues * gridWidthInValues;
            std::vector<float> cornerValues(totalValueCount, 0.0f);

            for (uint32_t z = 0; z < gridWidthInValues; z++)
            {
                for (uint32_t y = 0; y < gridWidthInValues; y++)
                {
                    for (uint32_t x = 0; x < gridWidthInValues; x++)
                    {
                        float3 pLocal = (float3(x, y, z) / float(gridWidth)) - 0.5f;
                        float sd;

                        // Create a Box.
                        {
                            float3 d = abs(pLocal) - float3(kHalfCheeseExtent);
                            float outsideDist = glm::length(float3(glm::max(d.x, 0.0f), glm::max(d.y, 0.0f), glm::max(d.z, 0.0f)));
                            float insideDist = glm::min(glm::max(glm::max(d.x, d.y), d.z), 0.0f);
                            sd = outsideDist + insideDist;
                        }

                        // Create holes.
                        for (uint32_t s = 0; s < kHoleCount; s++)
                        {
                            float4 holeData = holes[s];
                            sd = glm::max(sd, -(glm::length(pLocal - holeData.xyz) - holeData.w));
                        }

                        // We don't care about distance further away than the length of the diagonal of the unit cube where the SDF grid is defined.
                        cornerValues[x + gridWidthInValues * (y + gridWidthInValues * z)] = glm::clamp(sd, -glm::root_three<float>(), glm::root_three<float>());
                    }
                }
            }

            pSDFGrid->setValues(cornerValues, gridWidth, narrowBandThickness);
            return pSDFGrid;
        };

        pybind11::class_<SDFGrid, SDFGrid::SharedPtr> sdfGrid(m, "SDFGrid");
        sdfGrid.def(pybind11::init(pybind11::overload_cast<void>(&SDFGrid::create)));
        sdfGrid.def("loadValuesFromFile", &SDFGrid::loadValuesFromFile, "filename"_a, "narrowBandThickness"_a);
        sdfGrid.def_property("name", &SDFGrid::getName, &SDFGrid::setName);
        sdfGrid.def_static("createCheeseSDFGrid", createCheeseSDFGrid, "gridWidth"_a, "narrowBandThickness"_a, "seed"_a);
    }
}
