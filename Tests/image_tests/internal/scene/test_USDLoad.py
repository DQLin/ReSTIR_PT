import sys
sys.path.append('../..')
import os
from helpers import render_frames
from graphs.SceneDebugger import SceneDebugger as g
from falcor import *

m.addGraph(g)

# Spheres
m.loadScene('InstancedSpheresTestScene/instancing.pyscene')
render_frames(m, 'spheres', frames=[1])

# Attic
m.loadScene('Attic_NVIDIA/Attic_NVIDIA.pyscene')
render_frames(m, 'Attic', frames=[1])

exit()
