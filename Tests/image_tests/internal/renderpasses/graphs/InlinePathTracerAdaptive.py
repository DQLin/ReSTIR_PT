from falcor import *

def render_graph_InlinePathTracerAdaptive():
    g = RenderGraph("InlinePathTracerAdaptive")
    loadRenderPassLibrary("AccumulatePass.dll")
    loadRenderPassLibrary("GBuffer.dll")
    loadRenderPassLibrary("InlinePathTracer.dll")
    loadRenderPassLibrary("ToneMapper.dll")
    loadRenderPassLibrary("ImageLoader.dll")
    loadRenderPassLibrary('Utils.dll')

    InlinePathTracer = createPass("InlinePathTracer")
    g.addPass(InlinePathTracer, "InlinePathTracer")
    VBufferRT = createPass("VBufferRT", {'samplePattern': SamplePattern.Center, 'sampleCount': 16, 'useAlphaTest': True})
    g.addPass(VBufferRT, "VBufferRT")
    AccumulatePass = createPass("AccumulatePass", {'enabled': True, 'precisionMode': AccumulatePrecision.Single})
    g.addPass(AccumulatePass, "AccumulatePass")
    ToneMapper = createPass("ToneMapper", {'autoExposure': False, 'exposureCompensation': 0.0})
    g.addPass(ToneMapper, "ToneMapper")

    # Load a density map as an image in range [0,1] and scale it by 16.0
    DensityMap = createPass("ImageLoader", {'filename': 'Images/density-map.png', 'mips': False, 'srgb': False})
    g.addPass(DensityMap, 'DensityMap')
    DensityScaler = createPass("Composite", {'scaleA': 16.0, 'outputFormat': ResourceFormat.Unknown})
    g.addPass(DensityScaler, 'DensityScaler')
    g.addEdge('DensityMap.dst', 'DensityScaler.A')
    g.addEdge('DensityScaler.out', 'InlinePathTracer.sampleCount')

    g.addEdge("VBufferRT.vbuffer", "InlinePathTracer.vbuffer")
    g.addEdge("InlinePathTracer.color", "AccumulatePass.input")
    g.addEdge("AccumulatePass.output", "ToneMapper.src")
    g.markOutput("ToneMapper.dst")
    return g

InlinePathTracerAdaptive = render_graph_InlinePathTracerAdaptive()
try: m.addGraph(InlinePathTracerAdaptive)
except NameError: None
