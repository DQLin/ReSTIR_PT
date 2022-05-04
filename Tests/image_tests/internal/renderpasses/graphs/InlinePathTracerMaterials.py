from falcor import *

def render_graph_InlinePathTracerMaterials():
    g = RenderGraph("InlinePathTracer")
    loadRenderPassLibrary("AccumulatePass.dll")
    loadRenderPassLibrary("GBuffer.dll")
    loadRenderPassLibrary("InlinePathTracer.dll")
    loadRenderPassLibrary("ToneMapper.dll")

    InlinePathTracer = createPass("InlinePathTracer", {'samplesPerPixel': 1, 'maxBounces': 3})
    g.addPass(InlinePathTracer, "InlinePathTracer")
    VBufferRT = createPass("VBufferRT", {'samplePattern': SamplePattern.Stratified, 'sampleCount': 16})
    g.addPass(VBufferRT, "VBufferRT")
    AccumulatePass = createPass("AccumulatePass", {'enabled': True, 'precisionMode': AccumulatePrecision.Single})
    g.addPass(AccumulatePass, "AccumulatePass")
    ToneMapper = createPass("ToneMapper", {'autoExposure': False, 'exposureCompensation': 0.0})
    g.addPass(ToneMapper, "ToneMapper")

    g.addEdge("VBufferRT.vbuffer", "InlinePathTracer.vbuffer")
    g.addEdge("InlinePathTracer.color", "AccumulatePass.input")
    g.addEdge("AccumulatePass.output", "ToneMapper.src")
    g.markOutput("ToneMapper.dst")
    return g

InlinePathTracerMaterials = render_graph_InlinePathTracerMaterials()
try: m.addGraph(InlinePathTracerMaterials)
except NameError: None
