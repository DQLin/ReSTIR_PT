/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/
__exported import Reservoir;
__exported import EvalContext;
__exported import Utils.Sampling.SampleGeneratorInterface;
import Lights;
import Scene.Scene;

/** Resampled importance sampling state.
*/
struct RisState
{
    LightSample lightSample;    ///< Selected light sample.

    float weightSum;            ///< Sum of weights seen so far.
    float M;                    ///< Number of samples seen so far (float required for pairwise MIS).
    float weight;               ///< Probabilistic weight.
    float canonicalWeight;      ///< Canonical sample weight factor (required for pairwise MIS).

    /** Create an empty RIS state.
    */
    static RisState createEmpty()
    {
        RisState state = {};
        state.lightSample = LightSample::createInvalid();
        return state;
    }

    /** Create a reservoir from the current RIS state.
        An empty reservoir is returned if the weight is infinite or NaN.
        \return Returns a reservoir representing the current RIS state.
    */
    Reservoir toReservoir()
    {
        Reservoir reservoir;
        reservoir.lightSample = lightSample;
        reservoir.weight = weight;
        reservoir.M = uint(M);
        if (isinf(reservoir.weight) || isnan(reservoir.weight)) reservoir = Reservoir::createEmpty();
        return reservoir;
    }
};


/** Resample a light sample.
    This implements Algorithm 2 from the paper.
    \param[in,out] state RIS state.
    \param[in] lightSample Light sample to resample.
    \param[in] targetPdf Pdf of the sample in the target distribution (p-hat).
    \param[in] sourcePdf Pdf of the sample in the source distribution (p).
    \param[in,out] sg Sample generator.
    \return Returns true if new sample was selected.
*/
// used by initial sampling
bool streamingResampleStep<S : ISampleGenerator>(inout RisState state, const LightSample lightSample, float targetPdf, float sourcePdf, inout S sg)
{
    float sampleWeight = targetPdf / sourcePdf;

    state.weightSum += sampleWeight;
    state.M += 1.f;

    bool selectSample = sampleNext1D(sg) * state.weightSum < sampleWeight;

    if (selectSample)
    {
        state.lightSample = lightSample;
        state.weight = targetPdf;
    }

    // this return value does not matter since it is not used anywhere
    return selectSample;
}

/** Resample a light sample from a reservoir.
    This implements Algorithm 4 from the paper.
    \param[in,out] state RIS state.
    \param[in] reservoir Reservoir to resample.
    \param[in] targetPdf Pdf of the sample in the target distribution (p-hat).
    \param[in,out] sg Sample generator.
    \return Returns true if new sample was selected.
*/
// used by resampling
bool streamingResampleStep<S : ISampleGenerator>(inout RisState state, const Reservoir reservoir, float targetPdf, inout S sg, bool isCenterPixel)
{
    float sampleWeight = targetPdf * reservoir.weight * reservoir.M;

    state.weightSum += sampleWeight;
    state.M += reservoir.M;

    bool selectSample = sampleNext1D(sg) * state.weightSum < sampleWeight;

    if (selectSample)
    {
        state.lightSample = reservoir.lightSample;
        state.weight = targetPdf;
    }

    return selectSample;
}

/** Resample a light sample from a reservoir.
    This implements Algorithm 4 from the paper.
    \param[in,out] state RIS state.
    \param[in] reservoir Reservoir to resample.
    \param[in] evalContext Context for evaluating light samples.
    \param[in] lights Collection of lights.
    \param[in,out] sg Sample generator.
    \return Returns true if new sample was selected.
*/
// used by resampling

bool streamingResampleStep<S : ISampleGenerator>(inout RisState state, const Reservoir reservoir, const EvalContext evalContext, const Lights lights, inout S sg, bool isCenterPixel=true)
{
    const EvaluatedLightSample evaluatedLightSample = lights.evalLightSample(reservoir.lightSample, evalContext.pos);
    float targetPdf = evalContext.evalTargetFunction(evaluatedLightSample);
    return streamingResampleStep(state, reservoir, targetPdf, sg, isCenterPixel);
}

float mFactor(float q0, float q1)
{
    return q0 == 0.f ? 1.f : clamp(pow(min(q1 / q0, 1.f), 8.f), 0.f, 1.f);
}

float pairwiseMisWeight(float q0, float q1, float N0, float N1)
{
    return (q1 == 0.f) ? 0.f : (N0 * q0) / (q0 * N0 + q1 * N1);
}

bool streamingResampleStepMis<S : ISampleGenerator>(
    inout RisState state,
    const Reservoir canonicalReservoir, const LoadedLightSample canonicalLightSample, float canonicalTargetPdf, const EvalContext canonicalContext,
    const Reservoir candidateReservoir, const EvalContext candidateContext,
    uint k,
    const Lights lights,
    inout S sg)
{
    const LoadedLightSample candidateLightSample = lights.loadLightSample(candidateReservoir.lightSample);

    float candidateTargetPdf = candidateContext.evalTargetFunction(candidateLightSample);
    float candidateTargetPdfAtOther = canonicalContext.evalTargetFunction(candidateLightSample);
    float canonicalTargetPdfAtOther = candidateContext.evalTargetFunction(canonicalLightSample);

    float m0 =       pairwiseMisWeight(candidateTargetPdf,        candidateTargetPdfAtOther, candidateReservoir.M * k, canonicalReservoir.M);
    float m1 = 1.f - pairwiseMisWeight(canonicalTargetPdfAtOther, canonicalTargetPdf,        candidateReservoir.M * k, canonicalReservoir.M);

    float sampleWeight = candidateTargetPdfAtOther * candidateReservoir.weight * m0;
    state.M += candidateReservoir.M * min(mFactor(candidateTargetPdf, candidateTargetPdfAtOther), mFactor(canonicalTargetPdfAtOther, canonicalTargetPdf));
    state.weightSum += sampleWeight;
    state.canonicalWeight += m1;

    bool selectSample = sampleNext1D(sg) * state.weightSum < sampleWeight;

    if (selectSample)
    {
        state.lightSample = candidateReservoir.lightSample;
        state.weight = candidateTargetPdfAtOther;
    }

    return selectSample;
}


bool streamingResampleStepMisUnbiased<S : ISampleGenerator>(
    inout RisState state,
    const Reservoir canonicalReservoir, const LoadedLightSample canonicalLightSample, float canonicalTargetPdf, const EvalContext canonicalContext,
    const Reservoir candidateReservoir, const EvalContext candidateContext,
    uint k,
    const Lights lights,
    inout S sg)
{
    const LoadedLightSample candidateLightSample = lights.loadLightSample(candidateReservoir.lightSample);

    float candidateTargetPdf = candidateContext.evalTargetFunction(candidateLightSample);
    float candidateTargetPdfAtOther = canonicalContext.evalTargetFunction(candidateLightSample);;
    float canonicalTargetPdfAtOther = candidateContext.evalTargetFunction(canonicalLightSample);

    float canonicalVisibility = canonicalContext.evalVisibility(candidateLightSample);
    float candidateVisibility = candidateContext.evalVisibility(canonicalLightSample);

    candidateTargetPdfAtOther *= canonicalVisibility;
    canonicalTargetPdfAtOther *= candidateVisibility;

    float m0 = pairwiseMisWeight(candidateTargetPdf, candidateTargetPdfAtOther, candidateReservoir.M * k, canonicalReservoir.M);
    float m1 = 1.f - pairwiseMisWeight(canonicalTargetPdfAtOther, canonicalTargetPdf, candidateReservoir.M * k, canonicalReservoir.M);

    float sampleWeight = candidateTargetPdfAtOther * candidateReservoir.weight * m0;
    state.M += candidateReservoir.M * min(mFactor(candidateTargetPdf, candidateTargetPdfAtOther), mFactor(canonicalTargetPdfAtOther, canonicalTargetPdf));
    state.weightSum += sampleWeight;
    state.canonicalWeight += m1;

    bool selectSample = sampleNext1D(sg) * state.weightSum < sampleWeight;

    if (selectSample)
    {
        state.lightSample = candidateReservoir.lightSample;
        state.weight = candidateTargetPdfAtOther;
    }

    return selectSample;
}




bool streamingResampleFinalizeMis<S : ISampleGenerator>(inout RisState state, const Reservoir canonicalReservoir, float canonicalTargetPdf, inout S sg)
{
    float sampleWeight = canonicalTargetPdf * canonicalReservoir.weight * state.canonicalWeight;

    state.M += canonicalReservoir.M;
    state.weightSum += sampleWeight;

    bool selectSample = sampleNext1D(sg) * state.weightSum < sampleWeight;

    if (selectSample)
    {
        state.lightSample = canonicalReservoir.lightSample;
        state.weight = canonicalTargetPdf;
    }

    return selectSample;
}
