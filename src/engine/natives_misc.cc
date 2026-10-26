#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/canvas_hooks.h"
#include "engine/console.h"
#include "engine/net.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace rune {
    CanvasHooks& canvasHooks()
    {
        static CanvasHooks hooks;
        return hooks;
    }

    void postRenderHud(UObject* player, UObject* canvas, float textScale)
    {
        setTextScale(textScale);
        vm::Call(player, Name("PostRenderFlash")).obj(canvas).run();
        vm::Call(player, Name("PostRender")).obj(canvas).run();
        setTextScale(1.0f);
    }

    LinearColor canvasColor(UObject* canvas)
    {
        Color color = rune::canvas::DrawColor(canvas);
        float alpha = rune::canvas::Style(canvas) == 5 ? rune::canvas::AlphaScale(canvas) : 1.0f;
        return { static_cast<float>(color.r) / 255.0f, static_cast<float>(color.g) / 255.0f, static_cast<float>(color.b) / 255.0f, alpha };
    }

    namespace {
        float sinTab(int32_t angle)
        {
            return std::sin(static_cast<float>((angle >> 2) & 16383) * (2.0f * Pi / 16384.0f));
        }

        float cosTab(int32_t angle)
        {
            return sinTab(angle + 16384);
        }
    }

    Vector transformPointBy(const Vector& point, const Coords& coords)
    {
        Vector local = point - coords.origin;
        return { local.dot(coords.xAxis), local.dot(coords.yAxis), local.dot(coords.zAxis) };
    }

    Vector transformVectorBy(const Vector& vector, const Coords& coords)
    {
        return { vector.dot(coords.xAxis), vector.dot(coords.yAxis), vector.dot(coords.zAxis) };
    }

    Coords coordsTranspose(const Coords& coords)
    {
        return { -transformVectorBy(coords.origin, coords), { coords.xAxis.x, coords.yAxis.x, coords.zAxis.x }, { coords.xAxis.y, coords.yAxis.y, coords.zAxis.y }, { coords.xAxis.z, coords.yAxis.z, coords.zAxis.z } };
    }

    Coords coordsTransform(const Coords& coords, const Coords& by)
    {
        return { transformPointBy(coords.origin, by), transformVectorBy(coords.xAxis, by), transformVectorBy(coords.yAxis, by), transformVectorBy(coords.zAxis, by) };
    }

    Coords coordsRotate(Coords coords, const Rotator& rotation)
    {
        coords = coordsTransform(coords, { {}, { cosTab(rotation.yaw), sinTab(rotation.yaw), 0 }, { -sinTab(rotation.yaw), cosTab(rotation.yaw), 0 }, { 0, 0, 1 } });
        coords = coordsTransform(coords, { {}, { cosTab(rotation.pitch), 0, sinTab(rotation.pitch) }, { 0, 1, 0 }, { -sinTab(rotation.pitch), 0, cosTab(rotation.pitch) } });
        return coordsTransform(coords, { {}, { 1, 0, 0 }, { 0, cosTab(rotation.roll), -sinTab(rotation.roll) }, { 0, sinTab(rotation.roll), cosTab(rotation.roll) } });
    }

    Coords coordsUnrotate(Coords coords, const Rotator& rotation)
    {
        coords = coordsTransform(coords, { {}, { 1, 0, 0 }, { 0, cosTab(rotation.roll), sinTab(rotation.roll) }, { 0, -sinTab(rotation.roll), cosTab(rotation.roll) } });
        coords = coordsTransform(coords, { {}, { cosTab(rotation.pitch), 0, -sinTab(rotation.pitch) }, { 0, 1, 0 }, { sinTab(rotation.pitch), 0, cosTab(rotation.pitch) } });
        return coordsTransform(coords, { {}, { cosTab(rotation.yaw), -sinTab(rotation.yaw), 0 }, { sinTab(rotation.yaw), cosTab(rotation.yaw), 0 }, { 0, 0, 1 } });
    }

    Rotator orthoRotation(const Coords& coords)
    {
        Rotator rotation;
        rotation.pitch = static_cast<int32_t>(std::atan2(static_cast<double>(coords.xAxis.z), std::sqrt(static_cast<double>(coords.xAxis.x * coords.xAxis.x + coords.xAxis.y * coords.xAxis.y))) * 32768.0 / static_cast<double>(Pi));
        rotation.yaw = static_cast<int32_t>(std::atan2(static_cast<double>(coords.xAxis.y), static_cast<double>(coords.xAxis.x)) * 32768.0 / static_cast<double>(Pi));
        rotation.roll = 0;
        Coords basis = coordsUnrotate({ {}, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, rotation);
        rotation.roll = static_cast<int32_t>(std::atan2(static_cast<double>(coords.zAxis.dot(basis.yAxis)), static_cast<double>(coords.yAxis.dot(basis.yAxis))) * 32768.0 / static_cast<double>(Pi));
        return rotation;
    }

    namespace {
        BlendStyle styleOf(uint8_t style)
        {
            switch (style) {
            case 0:
                return BlendStyle::None;
            case 2:
                return BlendStyle::Masked;
            case 3:
                return BlendStyle::Translucent;
            case 4:
                return BlendStyle::Modulated;
            case 5:
                return BlendStyle::AlphaBlend;
            case 6:
                return BlendStyle::Multiply;
            default:
                return BlendStyle::Normal;
            }
        }

        UFont* fontOf(UObject* canvasObject)
        {
            UFont* font = cast<UFont>(rune::canvas::Font(canvasObject));
            objects::ensureLoaded(font);
            return font;
        }

        bool swappedFont(UFont* font)
        {
            if (!font || iequals(config::language(), "INT")) {
                return false;
            }
            static const char* const names[] = { "RuneMed", "RuneBig", "RuneCred", "RuneCred2", "RuneLarge", "RuneButton" };
            for (const char* name : names) {
                if (iequals(font->name.str(), name)) {
                    return true;
                }
            }
            return false;
        }

        UFont* canvasFont(UObject* canvasObject)
        {
            UFont* font = fontOf(canvasObject);
            if (swappedFont(font)) {
                rune::canvas::Font(canvasObject) = rune::canvas::BigFont(canvasObject);
                font = fontOf(canvasObject);
            }
            return font;
        }

        BlendStyle textStyle(uint8_t style)
        {
            switch (style) {
            case 3:
                return BlendStyle::Translucent;
            case 4:
                return BlendStyle::Modulated;
            case 5:
                return BlendStyle::AlphaBlend;
            default:
                return BlendStyle::Normal;
            }
        }

        void wrappedPrint(UObject* self, uint8_t style, int32_t& xl, int32_t& yl, UFont* font, bool center, std::string_view text)
        {
            if (rune::canvas::ClipX(self) < 0 || rune::canvas::ClipY(self) < 0) {
                return;
            }
            if (swappedFont(font)) {
                font = cast<UFont>(rune::canvas::BigFont(self));
                objects::ensureLoaded(font);
            }
            if (!font) {
                return;
            }
            Canvas2D* target = canvasHooks().target;
            StringStyle stringStyle;
            stringStyle.color = canvasColor(self);
            stringStyle.blend = textStyle(style);
            stringStyle.spaceX = rune::canvas::SpaceX(self);
            stringStyle.orgX = rune::canvas::OrgX(self);
            stringStyle.orgY = rune::canvas::OrgY(self);
            xl = 0;
            yl = 0;
            while (true) {
                size_t cleanWordEnd = 0;
                int32_t testXL = static_cast<int32_t>(rune::canvas::CurX(self));
                int32_t cleanXL = 0;
                int32_t testYL = 0;
                int32_t cleanYL = 0;
                bool gotWord = false;
                for (size_t i = 0; i < text.size() && text[i] != '\n';) {
                    int32_t width = 0;
                    int32_t height = 0;
                    charSize(font, static_cast<uint8_t>(text[i]), width, height);
                    testXL += static_cast<int32_t>(rune::canvas::SpaceX(self) + static_cast<float>(width));
                    testYL = std::max(testYL, static_cast<int32_t>(rune::canvas::SpaceY(self)) + height);
                    if (static_cast<float>(testXL) > rune::canvas::ClipX(self)) {
                        break;
                    }
                    i++;
                    bool wordBreak = i >= text.size() || text[i] == ' ' || text[i] == '\n';
                    if (wordBreak || !gotWord) {
                        cleanWordEnd = i;
                        cleanXL = testXL;
                        cleanYL = testYL;
                        gotWord = gotWord || wordBreak;
                    }
                }
                if (cleanWordEnd == 0) {
                    return;
                }
                float top = rune::canvas::OrgY(self) + rune::canvas::CurY(self);
                if (style != 0 && target && top < static_cast<float>(rune::canvas::SizeY(self)) && top + static_cast<float>(cleanYL) > 0) {
                    int32_t lineX = center ? static_cast<int32_t>((rune::canvas::ClipX(self) - static_cast<float>(cleanXL)) * 0.5f + rune::canvas::CurX(self)) : static_cast<int32_t>(rune::canvas::CurX(self));
                    lineX += drawString(target, font, lineX, static_cast<int32_t>(rune::canvas::CurY(self)), text.substr(0, cleanWordEnd), stringStyle);
                    rune::canvas::CurX(self) = static_cast<float>(lineX);
                }
                rune::canvas::CurX(self) = 0;
                rune::canvas::CurY(self) += static_cast<float>(cleanYL);
                yl += cleanYL;
                xl = std::max(xl, cleanXL);
                text.remove_prefix(cleanWordEnd);
                while (!text.empty() && (text.front() == ' ' || text.front() == '\n')) {
                    text.remove_prefix(1);
                }
                if (text.empty()) {
                    return;
                }
            }
        }

        void nativeStrLen(UObject* self, Frame& stack, void*)
        {
            std::string text = vm::get<std::string>(stack);
            float* xl = vm::getRef<float>(stack);
            float* yl = vm::getRef<float>(stack);
            vm::finish(stack);
            int32_t oldX = static_cast<int32_t>(rune::canvas::CurX(self));
            int32_t oldY = static_cast<int32_t>(rune::canvas::CurY(self));
            rune::canvas::CurX(self) = 0;
            rune::canvas::CurY(self) = 0;
            int32_t width = 0;
            int32_t height = 0;
            wrappedPrint(self, 0, width, height, fontOf(self), false, text);
            rune::canvas::CurY(self) = static_cast<float>(oldY);
            rune::canvas::CurX(self) = static_cast<float>(oldX);
            *xl = static_cast<float>(width);
            *yl = static_cast<float>(height);
        }

        void nativeTextSize(UObject* self, Frame& stack, void*)
        {
            std::string text = vm::get<std::string>(stack);
            float* xl = vm::getRef<float>(stack);
            float* yl = vm::getRef<float>(stack);
            vm::finish(stack);
            UFont* font = canvasFont(self);
            if (!font) {
                return;
            }
            int32_t width = 0;
            int32_t height = 0;
            for (char c : text) {
                int32_t characterWidth = 0;
                int32_t characterHeight = 0;
                charSize(font, static_cast<uint8_t>(c), characterWidth, characterHeight);
                width += characterWidth;
                height = std::max(height, characterHeight);
            }
            *xl = static_cast<float>(width);
            *yl = static_cast<float>(height);
        }

        void nativeDrawText(UObject* self, Frame& stack, void*)
        {
            std::string text = vm::get<std::string>(stack);
            bool carriageReturn = vm::getBoolOpt(stack, true);
            vm::finish(stack);
            UFont* font = canvasFont(self);
            if (!font) {
                return;
            }
            int32_t xl = 0;
            int32_t yl = 0;
            uint8_t style = rune::canvas::Style(self);
            if (style != 0) {
                wrappedPrint(self, style, xl, yl, font, rune::canvas::bCenter(self) != 0, text);
            }
            rune::canvas::CurX(self) += static_cast<float>(xl);
            rune::canvas::CurYL(self) = std::max(rune::canvas::CurYL(self), static_cast<float>(yl));
            if (carriageReturn) {
                rune::canvas::CurX(self) = 0;
                rune::canvas::CurY(self) += rune::canvas::CurYL(self);
                rune::canvas::CurYL(self) = 0;
            }
        }

        void nativeDrawTextClipped(UObject* self, Frame& stack, void*)
        {
            std::string text = vm::get<std::string>(stack);
            bool checkHotKey = vm::getBoolOpt(stack, false);
            vm::finish(stack);
            UFont* font = canvasFont(self);
            Canvas2D* target = canvasHooks().target;
            if (!font || !target || rune::canvas::Style(self) == 0) {
                return;
            }
            StringStyle stringStyle;
            stringStyle.color = canvasColor(self);
            stringStyle.blend = textStyle(rune::canvas::Style(self));
            stringStyle.spaceX = rune::canvas::SpaceX(self);
            stringStyle.orgX = rune::canvas::OrgX(self);
            stringStyle.orgY = rune::canvas::OrgY(self);
            stringStyle.clip = true;
            stringStyle.clipX = rune::canvas::ClipX(self);
            stringStyle.clipY = rune::canvas::ClipY(self);
            stringStyle.hotKey = checkHotKey;
            drawString(target, font, static_cast<int32_t>(rune::canvas::CurX(self)), static_cast<int32_t>(rune::canvas::CurY(self)), text, stringStyle);
        }

        void nativeDrawTile(UObject* self, Frame& stack, void*)
        {
            UObject* texture = vm::get<UObject*>(stack);
            float xl = vm::get<float>(stack);
            float yl = vm::get<float>(stack);
            float u = vm::get<float>(stack);
            float v = vm::get<float>(stack);
            float ul = vm::get<float>(stack);
            float vl = vm::get<float>(stack);
            vm::finish(stack);
            UTexture* tex = cast<UTexture>(texture);
            Canvas2D* target = canvasHooks().target;
            uint8_t style = rune::canvas::Style(self);
            if (!tex) {
                return;
            }
            if (target && style != 0) {
                objects::ensureLoaded(tex);
                target->drawTile(tex, rune::canvas::OrgX(self) + rune::canvas::CurX(self), rune::canvas::OrgY(self) + rune::canvas::CurY(self), xl, yl, u, v, ul, vl, canvasColor(self), styleOf(style), !rune::canvas::bNoSmooth(self));
            }
            rune::canvas::CurX(self) += xl + rune::canvas::SpaceX(self);
            rune::canvas::CurYL(self) = std::max(rune::canvas::CurYL(self), yl);
        }

        void nativeDrawTileClipped(UObject* self, Frame& stack, void*)
        {
            UObject* texture = vm::get<UObject*>(stack);
            float xl = vm::get<float>(stack);
            float yl = vm::get<float>(stack);
            float u = vm::get<float>(stack);
            float v = vm::get<float>(stack);
            float ul = vm::get<float>(stack);
            float vl = vm::get<float>(stack);
            vm::finish(stack);
            UTexture* tex = cast<UTexture>(texture);
            Canvas2D* target = canvasHooks().target;
            uint8_t style = rune::canvas::Style(self);
            if (!tex || xl <= 0 || yl <= 0) {
                return;
            }
            objects::ensureLoaded(tex);
            float curX = rune::canvas::CurX(self);
            float curY = rune::canvas::CurY(self);
            float clipX = rune::canvas::ClipX(self);
            float clipY = rune::canvas::ClipY(self);
            if (curX < 0) {
                float c = curX * ul / xl;
                u -= c;
                ul += c;
                xl += curX;
                curX = 0;
                rune::canvas::CurX(self) = 0;
            }
            if (curY < 0) {
                float c = curY * vl / yl;
                v -= c;
                vl += c;
                yl += curY;
                curY = 0;
                rune::canvas::CurY(self) = 0;
            }
            if (xl > clipX - curX) {
                ul += (clipX - curX - xl) * ul / xl;
                xl = clipX - curX;
            }
            if (yl > clipY - curY) {
                vl += (clipY - curY - yl) * vl / yl;
                yl = clipY - curY;
            }
            if (target && style != 0 && xl > 0 && yl > 0) {
                target->drawTile(tex, rune::canvas::OrgX(self) + curX, rune::canvas::OrgY(self) + curY, xl, yl, u, v, ul, vl, canvasColor(self), styleOf(style), !rune::canvas::bNoSmooth(self));
            }
            rune::canvas::CurX(self) = rune::canvas::SpaceX(self) + rune::canvas::CurX(self) + xl;
            rune::canvas::CurYL(self) = std::max(rune::canvas::CurYL(self), yl);
        }

        void nativeDrawActor(UObject*, Frame& stack, void*)
        {
            UObject* a = vm::get<UObject*>(stack);
            bool wireframe = vm::getBool(stack);
            bool clearZ = vm::getBoolOpt(stack, false);
            vm::finish(stack);
            if (!a) {
                return;
            }
            actor::bHidden(a) = 0;
            if (canvasHooks().drawActor) {
                canvasHooks().drawActor(a, wireframe, clearZ, ActorFrame {});
            }
            actor::bHidden(a) = 1;
        }

        void nativeDrawClippedActor(UObject*, Frame& stack, void*)
        {
            UObject* a = vm::get<UObject*>(stack);
            bool wireframe = vm::getBool(stack);
            ActorFrame frame;
            frame.clipped = true;
            frame.width = vm::get<int32_t>(stack);
            frame.height = vm::get<int32_t>(stack);
            frame.x = vm::get<int32_t>(stack);
            frame.y = vm::get<int32_t>(stack);
            bool clearZ = vm::getBoolOpt(stack, false);
            vm::finish(stack);
            UObject* viewer = World::current ? World::current->player() : nullptr;
            frame.fov = viewer ? pawn::FovAngle(viewer) : canvasHooks().viewFov;
            if (!a) {
                return;
            }
            actor::bHidden(a) = 0;
            if (canvasHooks().drawActor) {
                canvasHooks().drawActor(a, wireframe, clearZ, frame);
            }
            actor::bHidden(a) = 1;
        }

        void nativeDrawPortal(UObject*, Frame& stack, void*)
        {
            int32_t x = vm::get<int32_t>(stack);
            int32_t y = vm::get<int32_t>(stack);
            int32_t width = vm::get<int32_t>(stack);
            int32_t height = vm::get<int32_t>(stack);
            UObject* camera = vm::get<UObject*>(stack);
            Vector location = vm::get<Vector>(stack);
            Rotator rotation = vm::get<Rotator>(stack);
            int32_t fov = vm::getOpt<int32_t>(stack, 90);
            bool clearZ = vm::getBoolOpt(stack, true);
            vm::finish(stack);
            if (canvasHooks().drawPortal) {
                canvasHooks().drawPortal(x, y, width, height, camera, location, rotation, static_cast<float>(fov), clearZ);
            }
        }

        void nativeZoneActors(UObject* self, Frame& stack, void*)
        {
            UClass* cls = cast<UClass>(vm::get<UObject*>(stack));
            UObject** out = vm::getRef<UObject*>(stack);
            vm::finish(stack);
            size_t index = 0;
            vm::iterate(stack, [&]() {
                while (World::current && index < World::current->actors.size()) {
                    UObject* a = World::current->actors[index++];
                    if (!a || a->destroyed || (cls && !a->isA(cls))) {
                        continue;
                    }
                    UObject* zone = region(a).zone;
                    if (zone != World::current->levelInfo ? zone == self : true) {
                        *out = a;
                        return true;
                    }
                }
                *out = nullptr;
                return false;
            });
        }

        void nativeGetLocalURL(UObject*, Frame& stack, void* result)
        {
            vm::finish(stack);
            vm::ret<std::string>(result, World::current ? World::current->url : std::string());
        }

        void nativeGetAddressURL(UObject*, Frame& stack, void* result)
        {
            vm::finish(stack);
            net::Driver* driver = net::active();
            if (driver && driver->server()) {
                vm::ret<std::string>(result, net::ipText(net::localIp()) + ":" + std::to_string(driver->port()));
                return;
            }
            std::string port = config::system().get("URL", "Port").value_or("7777");
            vm::ret<std::string>(result, ":" + std::to_string(std::atoi(port.c_str())));
        }

        void nativeDescribeSpec(UObject*, Frame& stack, void*)
        {
            int32_t index = vm::get<int32_t>(stack);
            UObject** start = vm::getRef<UObject*>(stack);
            UObject** end = vm::getRef<UObject*>(stack);
            int32_t* flags = vm::getRef<int32_t>(stack);
            int32_t* distance = vm::getRef<int32_t>(stack);
            int32_t* radius = vm::getRef<int32_t>(stack);
            int32_t* height = vm::getRef<int32_t>(stack);
            vm::finish(stack);
            *start = nullptr;
            *end = nullptr;
            *flags = 0;
            *distance = 0;
            *radius = 0;
            *height = 0;
            if (World::current && World::current->level && index >= 0 && static_cast<size_t>(index) < World::current->level->reachSpecs.size()) {
                const ReachSpec& spec = World::current->level->reachSpecs[static_cast<size_t>(index)];
                *start = spec.start;
                *end = spec.end;
                *flags = spec.reachFlags;
                *distance = spec.distance;
                *radius = spec.collisionRadius;
                *height = spec.collisionHeight;
            }
        }

        void nativeConsoleCommandBool(UObject* self, Frame& stack, void* result)
        {
            std::string command = vm::get<std::string>(stack);
            vm::finish(stack);
            bool handled = true;
            std::string output = console::execute(self, command, &handled);
            if (!output.empty()) {
                vm::Call(self, Name("Message")).obj(nullptr).s(output).n(Name("Console")).run();
            }
            vm::retBool(result, handled);
        }

        void nativePlayerConsoleCommand(UObject* self, Frame& stack, void* result)
        {
            std::string command = vm::get<std::string>(stack);
            vm::finish(stack);
            vm::ret<std::string>(result, playerpawn::Player(self) ? console::execute(self, command) : std::string());
        }

        void nativeClientTravel(UObject* self, Frame& stack, void*)
        {
            std::string url = vm::get<std::string>(stack);
            uint8_t type = vm::get<uint8_t>(stack);
            bool items = vm::getBool(stack);
            vm::finish(stack);
            if (!World::current || !playerpawn::Player(self)) {
                return;
            }
            vm::event(self, Name("PreClientTravel"));
            World::current->travelUrl = url;
            World::current->travelType = type;
            World::current->travelItems = items;
            World::current->travelPending = true;
            for (UObject* a : std::vector<UObject*>(World::current->actors)) {
                if (a && !a->destroyed) {
                    vm::Call preTravel(a, Name("PreTravel"));
                    if (preTravel.valid()) {
                        preTravel.run();
                    }
                }
            }
        }

        void nativeUpdateURL(UObject*, Frame& stack, void*)
        {
            std::string option = vm::get<std::string>(stack);
            std::string value = vm::get<std::string>(stack);
            bool saveDefault = vm::getBool(stack);
            vm::finish(stack);
            if (World::current) {
                std::string& url = World::current->url;
                std::string replaced;
                bool found = false;
                size_t mark = url.find('?');
                replaced = url.substr(0, mark);
                while (mark != std::string::npos) {
                    size_t next = url.find('?', mark + 1);
                    std::string item = url.substr(mark + 1, next == std::string::npos ? std::string::npos : next - mark - 1);
                    if (iequals(item.substr(0, item.find('=')), option)) {
                        item = option + "=" + value;
                        found = true;
                    }
                    replaced += "?" + item;
                    mark = next;
                }
                if (!found) {
                    replaced += "?" + option + "=" + value;
                }
                url = replaced;
            }
            if (saveDefault) {
                config::user().set("DefaultPlayer", option, value);
            }
        }

        void nativeGetDefaultURL(UObject*, Frame& stack, void* result)
        {
            std::string option = vm::get<std::string>(stack);
            vm::finish(stack);
            auto value = config::user().get("DefaultPlayer", option);
            vm::ret<std::string>(result, value ? *value : std::string());
        }

        void nativeResetKeyboard(UObject*, Frame& stack, void*)
        {
            vm::finish(stack);
            config::resetSection(Name("User"), "Engine.Input");
            console::loadBindings();
        }

        void nativeGetEntryLevel(UObject*, Frame& stack, void* result)
        {
            vm::finish(stack);
            World* level = World::entryLevel ? World::entryLevel : World::current;
            vm::ret<UObject*>(result, level ? level->levelInfo : nullptr);
        }

        void nativeCopyToClipboard(UObject*, Frame& stack, void*)
        {
            std::string text = vm::get<std::string>(stack);
            vm::finish(stack);
            if (canvasHooks().clipboardCopy) {
                canvasHooks().clipboardCopy(text);
            }
        }

        void nativePasteFromClipboard(UObject*, Frame& stack, void* result)
        {
            vm::finish(stack);
            vm::ret<std::string>(result, canvasHooks().clipboardPaste ? canvasHooks().clipboardPaste() : std::string());
        }

        void nativeParseKillMessage(UObject*, Frame& stack, void* result)
        {
            std::string killer = vm::get<std::string>(stack);
            std::string victim = vm::get<std::string>(stack);
            std::string weapon = vm::get<std::string>(stack);
            std::string message = vm::get<std::string>(stack);
            vm::finish(stack);
            auto replace = [&message](const std::string& token, const std::string& value) {
                size_t at = message.find(token);
                if (at != std::string::npos) {
                    message.replace(at, token.size(), value);
                }
            };
            replace("%k", killer);
            replace("%o", victim);
            replace("%w", weapon);
            vm::ret<std::string>(result, message);
        }

        void warpBy(Frame& stack, const Coords& coords)
        {
            Vector* location = vm::getRef<Vector>(stack);
            Vector* velocity = vm::getRef<Vector>(stack);
            Rotator* rotation = vm::getRef<Rotator>(stack);
            vm::finish(stack);
            *location = transformPointBy(*location, coords);
            *velocity = transformVectorBy(*velocity, coords);
            *rotation = orthoRotation(coordsTransform(coordsUnrotate({ {}, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, *rotation), coords));
        }

        void nativeWarp(UObject* self, Frame& stack, void*)
        {
            warpBy(stack, coordsTranspose(warpzoneinfo::WarpCoords(self)));
        }

        void nativeUnWarp(UObject* self, Frame& stack, void*)
        {
            warpBy(stack, warpzoneinfo::WarpCoords(self));
        }

        void nativeAttachDecal(UObject* self, Frame& stack, void* result)
        {
            float traceDistance = vm::get<float>(stack);
            Vector decalDir = vm::getOpt<Vector>(stack, Vector {});
            vm::finish(stack);
            vm::retBool(result, World::current && attachDecal(*World::current, self, traceDistance, decalDir));
        }

        void nativeDetachDecal(UObject* self, Frame& stack, void*)
        {
            vm::finish(stack);
            if (World::current) {
                detachDecal(*World::current, self);
            }
        }

        struct PlayerStats {
            std::unordered_map<uint8_t, int32_t> ints;
            std::unordered_map<uint8_t, float> floats;
            std::unordered_set<uint8_t> achievements;
        };

        PlayerStats& playerStats()
        {
            static PlayerStats stats;
            return stats;
        }

        void nativeModifyIntStat(UObject*, Frame& stack, void*)
        {
            uint8_t stat = vm::get<uint8_t>(stack);
            int32_t delta = vm::get<int32_t>(stack);
            vm::finish(stack);
            playerStats().ints[stat] += delta;
        }

        void nativeModifyFloatStat(UObject*, Frame& stack, void*)
        {
            uint8_t stat = vm::get<uint8_t>(stack);
            float delta = vm::get<float>(stack);
            vm::finish(stack);
            playerStats().floats[stat] += delta;
        }

        void nativeGetIntStat(UObject*, Frame& stack, void* result)
        {
            uint8_t stat = vm::get<uint8_t>(stack);
            vm::finish(stack);
            vm::ret<int32_t>(result, playerStats().ints[stat]);
        }

        void nativeGetFloatStat(UObject*, Frame& stack, void* result)
        {
            uint8_t stat = vm::get<uint8_t>(stack);
            vm::finish(stack);
            vm::ret<float>(result, playerStats().floats[stat]);
        }

        void nativeUnlockAchievement(UObject*, Frame& stack, void*)
        {
            uint8_t achievement = vm::get<uint8_t>(stack);
            vm::finish(stack);
            playerStats().achievements.insert(achievement);
        }

        void nativeGetAchievement(UObject*, Frame& stack, void* result)
        {
            uint8_t achievement = vm::get<uint8_t>(stack);
            vm::finish(stack);
            vm::retBool(result, playerStats().achievements.count(achievement) != 0);
        }

        LinearColor lineColor(float r, float g, float b)
        {
            return { std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f), std::clamp(b, 0.0f, 1.0f), 1.0f };
        }

        Vector viewSpace(const Vector& point)
        {
            const CanvasHooks& hooks = canvasHooks();
            Coords axes = rotationAxes(hooks.viewRotation);
            Vector offset = point - hooks.viewLocation;
            return { offset.dot(axes.yAxis), -offset.dot(axes.zAxis), offset.dot(axes.xAxis) };
        }

        Vector screenPoint(UObject* canvas, const Vector& view)
        {
            float halfX = static_cast<float>(rune::canvas::SizeX(canvas)) * 0.5f;
            float halfY = static_cast<float>(rune::canvas::SizeY(canvas)) * 0.5f;
            float projection = halfX / std::tan(canvasHooks().viewFov * Pi / 360.0f);
            float rz = 1.0f / view.z;
            return { halfX + view.x * projection * rz, halfY + view.y * projection * rz, rz };
        }

        void nativeDrawLine(UObject* self, Frame& stack, void*)
        {
            float x1 = vm::get<float>(stack);
            float y1 = vm::get<float>(stack);
            float x2 = vm::get<float>(stack);
            float y2 = vm::get<float>(stack);
            float r = vm::get<float>(stack);
            float g = vm::get<float>(stack);
            float b = vm::get<float>(stack);
            vm::finish(stack);
            Canvas2D* target = canvasHooks().target;
            if (target && rune::canvas::Style(self) != 0) {
                target->drawLine(x1, y1, x2, y2, lineColor(r, g, b));
            }
        }

        void nativeDrawLine3D(UObject* self, Frame& stack, void*)
        {
            Vector p1 = vm::get<Vector>(stack);
            Vector p2 = vm::get<Vector>(stack);
            float r = vm::get<float>(stack);
            float g = vm::get<float>(stack);
            float b = vm::get<float>(stack);
            vm::finish(stack);
            Canvas2D* target = canvasHooks().target;
            if (!target || rune::canvas::Style(self) == 0 || !canvasHooks().hasView) {
                return;
            }
            Vector v1 = viewSpace(p1);
            Vector v2 = viewSpace(p2);
            if ((v2 - v1).size() < 0.001f) {
                return;
            }
            if (v1.z < 1.0f) {
                if (v2.z < 1.0f) {
                    return;
                }
                v1 = v2 + (v1 - v2) * ((v2.z - 1.0f) / (v2.z - v1.z));
            } else if (v2.z < 1.0f) {
                v2 = v1 + (v2 - v1) * ((v1.z - 1.0f) / (v1.z - v2.z));
            }
            Vector s1 = screenPoint(self, v1);
            Vector s2 = screenPoint(self, v2);
            target->drawLine(s1.x, s1.y, s2.x, s2.y, lineColor(r, g, b));
        }

        void nativePlayerNetworkAddress(UObject* self, Frame& stack, void* result)
        {
            vm::finish(stack);
            std::string address;
            net::Driver* driver = net::active();
            net::Connection* connection = driver && driver->server() ? driver->connectionOf(self) : nullptr;
            if (connection) {
                address = net::ipText(connection->address.ip) + ":" + std::to_string(connection->address.port);
            }
            vm::ret<std::string>(result, address);
        }

        void nativeNetworkNumber(UObject*, Frame& stack, void* result)
        {
            vm::finish(stack);
            vm::ret<std::string>(result, net::active() ? net::ipText(net::localIp()) : std::string());
        }

        void nativeTransformPoint(UObject* self, Frame& stack, void*)
        {
            Vector point = vm::get<Vector>(stack);
            int32_t* x = vm::getRef<int32_t>(stack);
            int32_t* y = vm::getRef<int32_t>(stack);
            vm::finish(stack);
            if (!canvasHooks().hasView) {
                return;
            }
            Vector view = viewSpace(point);
            if (view.z <= 1.0f) {
                *x = -1000;
                *y = -1000;
                return;
            }
            Vector screen = screenPoint(self, view);
            *x = static_cast<int32_t>(screen.x);
            *y = static_cast<int32_t>(screen.y);
        }
    }

    void registerMiscNatives()
    {
        vm::registerNative(464, nativeStrLen);
        vm::registerNative(465, nativeDrawText);
        vm::registerNative(466, nativeDrawTile);
        vm::registerNative(467, nativeDrawActor);
        vm::registerNative(468, nativeDrawTileClipped);
        vm::registerNative(469, nativeDrawTextClipped);
        vm::registerNative(470, nativeTextSize);
        vm::registerNative(471, nativeDrawClippedActor);
        vm::registerNative(480, nativeDrawPortal);
        vm::registerNative(490, nativeDrawLine);
        vm::registerNative(491, nativeDrawLine3D);
        vm::registerNative(492, nativeTransformPoint);
        vm::registerNative(308, nativeZoneActors);
        vm::registerNative("LevelInfo", "GetLocalURL", nativeGetLocalURL);
        vm::registerNative("LevelInfo", "GetAddressURL", nativeGetAddressURL);
        vm::registerNative(519, nativeDescribeSpec);
        vm::registerNative("Console", "ConsoleCommand", nativeConsoleCommandBool);
        vm::registerNative("PlayerPawn", "ConsoleCommand", nativePlayerConsoleCommand);
        vm::registerNative("PlayerPawn", "ClientTravel", nativeClientTravel);
        vm::registerNative(546, nativeUpdateURL);
        vm::registerNative("PlayerPawn", "GetDefaultURL", nativeGetDefaultURL);
        vm::registerNative(544, nativeResetKeyboard);
        vm::registerNative("PlayerPawn", "GetEntryLevel", nativeGetEntryLevel);
        vm::registerNative("PlayerPawn", "CopyToClipboard", nativeCopyToClipboard);
        vm::registerNative("PlayerPawn", "PasteFromClipboard", nativePasteFromClipboard);
        vm::registerNative("PlayerPawn", "GetPlayerNetworkAddress", nativePlayerNetworkAddress);
        vm::registerNative("PlayerPawn", "ModifyIntStat", nativeModifyIntStat);
        vm::registerNative("PlayerPawn", "ModifyFloatStat", nativeModifyFloatStat);
        vm::registerNative("PlayerPawn", "GetIntStat", nativeGetIntStat);
        vm::registerNative("PlayerPawn", "GetFloatStat", nativeGetFloatStat);
        vm::registerNative("PlayerPawn", "UnlockAchievement", nativeUnlockAchievement);
        vm::registerNative("PlayerPawn", "GetAchievement", nativeGetAchievement);
        vm::registerNative("GameInfo", "GetNetworkNumber", nativeNetworkNumber);
        vm::registerNative("GameInfo", "ParseKillMessage", nativeParseKillMessage);
        vm::registerNative(314, nativeWarp);
        vm::registerNative(315, nativeUnWarp);
        vm::registerNative("Decal", "AttachDecal", nativeAttachDecal);
        vm::registerNative("Decal", "DetachDecal", nativeDetachDecal);
    }
}
