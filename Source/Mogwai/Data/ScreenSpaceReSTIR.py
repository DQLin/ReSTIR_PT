from falcor import *

def render_graph_ScreenSpaceReSTIRGraph():
    g = RenderGraph("ScreenSpaceReSTIR")
    loadRenderPassLibrary("AccumulatePass.dll")
    loadRenderPassLibrary("GBuffer.dll")
    loadRenderPassLibrary("ScreenSpaceReSTIRPass.dll")
    loadRenderPassLibrary("ToneMapper.dll")
    GBufferRT = createPass("GBufferRT")
    g.addPass(GBufferRT, "GBufferRT")
    ScreenSpaceReSTIRPass = createPass("ScreenSpaceReSTIRPass")
    g.addPass(ScreenSpaceReSTIRPass, "ScreenSpaceReSTIRPass")
    AccumulatePass = createPass("AccumulatePass", {'enabled': False, 'precisionMode': AccumulatePrecision.Single})
    g.addPass(AccumulatePass, "AccumulatePass")
    ToneMapper = createPass("ToneMapper", {'autoExposure': False, 'exposureCompensation': 0.0})
    g.addPass(ToneMapper, "ToneMapper")
    g.addEdge("GBufferRT.vbuffer", "ScreenSpaceReSTIRPass.vbuffer")
    g.addEdge("GBufferRT.mvec", "ScreenSpaceReSTIRPass.motionVectors")
    g.addEdge("ScreenSpaceReSTIRPass.color", "AccumulatePass.input")
    g.addEdge("AccumulatePass.output", "ToneMapper.src")
    g.markOutput("ToneMapper.dst")
    return g

ScreenSpaceReSTIRGraph = render_graph_ScreenSpaceReSTIRGraph()
try: m.addGraph(ScreenSpaceReSTIRGraph)
except NameError: None
