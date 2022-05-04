IMAGE_TEST = {
    'tags': ['convergence'],
    'timeout': 3600
}

import sys
sys.path.append('../..')
from helpers import render_frames
from graphs.InlinePathTracer import InlinePathTracer as g
from falcor import *

m.addGraph(g)
m.loadScene('TestScenes/ConvergenceTest.pyscene')

BOUNCES = [0, 3]
LIGHTS = ['all', 'emissive', 'env', 'analytic']
MODES = ['noNEE', 'NEE', 'NEEMIS']
SPP = 16 * 1024

def render_image(bounces, lights, mode):
    name = f'{bounces}-bounce-{lights}-light-{mode}'

    g.updatePass('InlinePathTracer', {
        'samplesPerPixel' : 1,
        'maxSurfaceBounces': bounces,
        'useNEE': mode == 'NEE' or mode == 'NEEMIS',
        'useMIS': mode == 'NEEMIS'
    })

    m.scene.renderSettings = SceneRenderSettings(
        useEnvLight = lights == 'all' or lights == 'env',
        useAnalyticLights = lights == 'all' or lights == 'analytic',
        useEmissiveLights = lights == 'all' or lights == 'emissive',
        useGridVolumes = False
    )

    render_frames(m, name, frames=[SPP])


for bounces in BOUNCES:
    for lights in LIGHTS:
        for mode in MODES:
            render_image(bounces, lights, mode)

exit()
