import sys
sys.path.append('../..')
from helpers import render_frames
from graphs.InlinePathTracerDielectrics import InlinePathTracerDielectrics as g
from falcor import *

m.addGraph(g)
m.loadScene('TestScenes/NestedDielectrics.pyscene')

# default
render_frames(m, 'default', frames=[1,256])

exit()
