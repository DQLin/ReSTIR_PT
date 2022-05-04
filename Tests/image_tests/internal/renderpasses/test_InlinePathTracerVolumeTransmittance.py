import sys
sys.path.append('../..')
from helpers import render_frames
from graphs.InlinePathTracer import InlinePathTracer as g
from falcor import *

m.addGraph(g)
m.loadScene('TestScenes/VolumeTransmittanceTest.pyscene')

# default
render_frames(m, 'default', frames=[1,4,16])

estimators = [TransmittanceEstimator.DeltaTracking, TransmittanceEstimator.RatioTracking, TransmittanceEstimator.UnbiasedRayMarching, TransmittanceEstimator.BiasedRayMarching, TransmittanceEstimator.RatioTrackingLocalMajorant]
for estimator in estimators:
    g.getPass('AccumulatePass').reset()
    g.updatePass('InlinePathTracer', {'samplesPerPixel': 1, 'gridVolumeSamplerOptions': GridVolumeSamplerOptions(transmittanceEstimator=estimator)})
    render_frames(m, str(estimator), frames=[1,4,16])

exit()
