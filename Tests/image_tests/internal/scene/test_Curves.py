import sys
sys.path.append('../..')
import os
from helpers import render_frames
from graphs.SceneDebugger import SceneDebugger as g
from falcor import *

m.addGraph(g)
m.loadScene('CurveTest/two_curves.pyscene')

modes = [
    SceneDebuggerMode.FaceNormal,
    SceneDebuggerMode.ShadingNormal,
    SceneDebuggerMode.ShadingTangent,
    SceneDebuggerMode.ShadingBitangent,
    SceneDebuggerMode.FrontFacingFlag,
    SceneDebuggerMode.BackfacingShadingNormal,
    SceneDebuggerMode.TexCoords,
    SceneDebuggerMode.HitType,
    SceneDebuggerMode.InstanceID,
    SceneDebuggerMode.MaterialID,
    SceneDebuggerMode.MeshID,
    SceneDebuggerMode.BlasID,
    SceneDebuggerMode.CurveID,
    SceneDebuggerMode.InstancedGeometry
    ]

p = g.getPass('SceneDebugger')
for mode in modes:
    p.mode = mode
    render_frames(m, 'mode.' + str(mode), frames=[1])

exit()
