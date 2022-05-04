from falcor import *

def render_graph_ScreenSpaceReSTIR():
    g = RenderGraph("ScreenSpaceReSTIR")
    loadRenderPassLibrary("GBuffer.dll")
    loadRenderPassLibrary("ScreenSpaceReSTIRPass.dll")
    loadRenderPassLibrary("ToneMapper.dll")
    ToneMappingPass = createPass("ToneMapper", {'autoExposure': False, 'exposureCompensation': 0.0})
    g.addPass(ToneMappingPass, "ToneMappingPass")
    ScreenSpaceReSTIRPass = createPass("ScreenSpaceReSTIRPass", {'useVBuffer': False})
    g.addPass(ScreenSpaceReSTIRPass, "ScreenSpaceReSTIRPass")
    GBufferRT = createPass("GBufferRT")
    g.addPass(GBufferRT, "GBufferRT")
    g.addEdge("GBufferRT.vbuffer", "ScreenSpaceReSTIRPass.vbuffer")
    g.addEdge("GBufferRT.mvec", "ScreenSpaceReSTIRPass.motionVectors")
    g.addEdge("ScreenSpaceReSTIRPass.color", "ToneMappingPass.src")
    g.markOutput("ToneMappingPass.dst")
    g.markOutput("ScreenSpaceReSTIRPass.color")
    g.markOutput("ScreenSpaceReSTIRPass.emission")
    g.markOutput("ScreenSpaceReSTIRPass.diffuseReflectance")
    g.markOutput("ScreenSpaceReSTIRPass.diffuseIllumination")
    g.markOutput("ScreenSpaceReSTIRPass.specularReflectance")
    g.markOutput("ScreenSpaceReSTIRPass.specularIllumination")
    return g

ScreenSpaceReSTIR = render_graph_ScreenSpaceReSTIR()
try: m.addGraph(ScreenSpaceReSTIR)
except NameError: None
