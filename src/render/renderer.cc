#include "render/renderer.h"

#include "engine/package.h"

namespace rune {
    RendererKind rendererKindOf(std::string_view className)
    {
        return iequals(className, rendererClass(RendererKind::Software)) ? RendererKind::Software : RendererKind::Hardware;
    }

    const char* rendererClass(RendererKind device)
    {
        return device == RendererKind::Software ? "SoftDrv.SoftwareRenderDevice" : "D3DDrv.D3DRenderDevice";
    }
}
