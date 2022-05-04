import sys
sys.path.append('../..')
from helpers import render_frames
from graphs.ScreenSpaceReSTIR import ScreenSpaceReSTIR as g
from falcor import *

m.addGraph(g)
m.loadScene('Arcade/Arcade.pyscene')

render_frames(m, 'default', frames=[32])

exit()
