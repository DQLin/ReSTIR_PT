import sys
sys.path.append('../..')
import os
from helpers import render_frames
from graphs.SceneDebugger import SceneDebugger as g
from falcor import *

m.addGraph(g)

# Camera, Skinned Cylinder, Cube
m.loadScene('AnimationTestScene/RigidBody_SkelMesh_Cylinder.usd')
render_frames(m, 'usd_anim', frames=[90])

exit()
