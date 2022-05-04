import sys
sys.path.append('../..')
from helpers import render_frames

exec(open('../../../../Source/Mogwai/Data/ExamplePathTracerWithPost.py').read())

# default
render_frames(m, 'default', frames=[1,4,16])

exit()
