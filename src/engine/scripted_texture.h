#pragma once

namespace rune {
    class UTexture;

    namespace scripted {
        bool isScripted(UTexture* texture);
        void tick(UTexture* texture);
    }

    void registerScriptedTextureNatives();
}
