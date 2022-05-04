import sys
sys.path.append('../..')
import os
from helpers import render_frames
from graphs.TestRtProgram import TestRtProgramGraph as g
from falcor import *

m.addGraph(g)

# Load scene with triangles, curves, and custom primitives
m.loadScene('CurveTest/two_curves.pyscene')

# Add some custom primitives
p = g.getPass('TestRtProgram')
p.addCustomPrimitive()
p.addCustomPrimitive()
p.addCustomPrimitive()
render_frames(m, 'add_custom', frames=[1])

# Move some custom primitives
p.moveCustomPrimitive()
p.moveCustomPrimitive()
p.moveCustomPrimitive()
p.moveCustomPrimitive()
render_frames(m, 'move_custom', frames=[1])

# Remove some custom primitives
p.removeCustomPrimitive(2)
p.removeCustomPrimitive(0)
render_frames(m, 'remove_custom', frames=[1])

exit()
