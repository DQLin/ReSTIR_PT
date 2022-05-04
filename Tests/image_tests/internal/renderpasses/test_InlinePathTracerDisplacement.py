import sys
sys.path.append('../..')
from helpers import render_frames
from graphs.InlinePathTracer import InlinePathTracer as g
from falcor import *

m.addGraph(g)
m.loadScene('TestScenes/CornellBoxDisplaced.pyscene')

# default
render_frames(m, 'default', frames=[1,256])

exit()
