import sys
sys.path.append('../..')
from helpers import render_frames
from graphs.InlinePathTracer import InlinePathTracer as g
from falcor import *

m.addGraph(g)
m.loadScene('Arcade/Arcade.pyscene')

# default
render_frames(m, 'default', frames=[128])

# schedulingMode
schedulingModes = [SchedulingMode.QueueInline, SchedulingMode.SimpleInline, SchedulingMode.ReorderTraceRay, SchedulingMode.ReorderInline]
for schedulingMode in schedulingModes:
    g.getPass('AccumulatePass').reset()
    g.updatePass('InlinePathTracer', {'samplesPerPixel': 1, 'schedulingMode': schedulingMode})
    render_frames(m, str(schedulingMode), frames=[1,4,16])

exit()
