#include "render/actor_shadow.h"

#include "core/objects.h"
#include "engine/props_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rune {
    namespace {
        struct ClipPlane {
            float x;
            float y;
            float z;
            float w;
        };

        const ClipPlane clipPlanes[6] = {
            { -1, 0, 0, -64 },
            { 1, 0, 0, -64 },
            { 0, -1, 0, -64 },
            { 0, 1, 0, -64 },
            { 0, 0, 1, -50 },
            { 0, 0, -1, -450 },
        };

        int32_t raster[shadow::TextureSize][2] = {};

        int32_t appFloor(float value)
        {
            return static_cast<int32_t>(std::nearbyint(static_cast<double>(value) - 0.5));
        }

        float planeDot(const Plane& plane, const Vector& point)
        {
            return plane.x * point.x + plane.y * point.y + plane.z * point.z - plane.w;
        }

        Vector toShadow(const shadow::Projection& projection, const Vector& point)
        {
            Vector local = point - projection.origin;
            return { local.dot(projection.xAxis), local.dot(projection.yAxis), local.dot(projection.zAxis) };
        }

        Vector fromShadow(const shadow::Projection& projection, const Vector& point)
        {
            return projection.origin + projection.xAxis * point.x + projection.yAxis * point.y + projection.zAxis * point.z;
        }

        int clipAgainst(const ClipPlane& plane, const std::vector<Vector>& source, std::vector<Vector>& dest)
        {
            dest.clear();
            size_t count = source.size();
            if (count == 0) {
                return 0;
            }
            std::vector<float> dist(count);
            for (size_t i = 0; i < count; i++) {
                dist[i] = plane.x * source[i].x + plane.y * source[i].y + plane.z * source[i].z - plane.w;
            }
            size_t previous = count - 1;
            for (size_t i = 0; i < count; i++) {
                if (!std::signbit(dist[previous])) {
                    dest.push_back(source[previous]);
                }
                if (static_cast<double>(dist[i] * dist[previous]) < 0.0) {
                    float t = dist[previous] / (dist[previous] - dist[i]);
                    dest.push_back((source[i] - source[previous]) * t + source[previous]);
                }
                previous = i;
            }
            return static_cast<int>(dest.size());
        }

        int clipToShadow(const shadow::Projection& projection, const Vector* points, size_t count, std::vector<Vector>& out)
        {
            std::vector<Vector> current(count);
            for (size_t i = 0; i < count; i++) {
                current[i] = toShadow(projection, points[i]);
            }
            std::vector<Vector> next;
            for (const ClipPlane& plane : clipPlanes) {
                if (clipAgainst(plane, current, next) == 0) {
                    out.clear();
                    return 0;
                }
                current.swap(next);
            }
            out.swap(current);
            return static_cast<int>(out.size());
        }

        void rasterPolygon(Vector* points, int count, int32_t& startY, int32_t& endY)
        {
            int32_t yInt[16];
            for (int i = 0; i < count; i++) {
                yInt[i] = appFloor(points[i].y);
            }
            startY = endY = yInt[0];
            for (int i = 1; i < count; i++) {
                if (yInt[i] < startY) {
                    startY = yInt[i];
                } else if (yInt[i] > endY) {
                    endY = yInt[i];
                }
            }
            if (startY < 0 || endY > shadow::TextureSize) {
                startY = std::clamp(startY, 0, shadow::TextureSize - 1);
                endY = std::clamp(endY, 0, shadow::TextureSize - 1);
                for (int i = 0; i < count; i++) {
                    yInt[i] = std::clamp(yInt[i], 0, shadow::TextureSize - 1);
                    points[i].y = std::clamp(points[i].y, 0.0f, static_cast<float>(shadow::TextureSize - 1));
                }
            }
            int p0 = 0;
            int p1 = 1;
            for (int i = 0; i < count; i++) {
                if (yInt[p0] != yInt[p1]) {
                    int top = p0;
                    int bottom = p1;
                    int index = 0;
                    if (yInt[p1] > yInt[p0]) {
                        bottom = p1;
                        top = p0;
                        index = 1;
                    } else {
                        bottom = p0;
                        top = p1;
                        index = 0;
                    }
                    double yAdjust = static_cast<double>(yInt[top]) - static_cast<double>(points[top].y);
                    double fdx = 65536.0 * static_cast<double>(points[bottom].x - points[top].x) / static_cast<double>(points[bottom].y - points[top].y);
                    uint32_t x = static_cast<uint32_t>(appFloor(static_cast<float>(65536.0 * static_cast<double>(points[top].x) + yAdjust * fdx)));
                    uint32_t dx = static_cast<uint32_t>(appFloor(static_cast<float>(fdx)));
                    int32_t row = yInt[top];
                    for (int32_t remaining = yInt[bottom] - yInt[top]; remaining > 0; remaining--) {
                        x += dx;
                        int32_t value = static_cast<int32_t>(x) >> 16;
                        if (value < 0) {
                            value = 0;
                        }
                        if (value >= 128) {
                            value = 127;
                        }
                        if (row >= 0 && row < shadow::TextureSize) {
                            raster[row][index] = value;
                        }
                        row++;
                    }
                }
                p0++;
                p1 = (p1 + 1) % count;
            }
        }

        void drawPolygon(UTexture* texture, Vector* points, int count)
        {
            if (count > 16 || texture->mips.empty()) {
                return;
            }
            int32_t startY = 0;
            int32_t endY = 0;
            rasterPolygon(points, count, startY, endY);
            std::vector<uint8_t>& data = texture->mips[0].data;
            int32_t usize = bitmap::USize(texture);
            for (int32_t i = startY; i < endY; i++) {
                int32_t span = raster[i][1] - raster[i][0];
                uint8_t color = static_cast<uint8_t>(128.0 - static_cast<double>(i) * 0.75);
                if (span > 0) {
                    size_t at = static_cast<size_t>(i * usize + raster[i][0]);
                    if (at + static_cast<size_t>(span) <= data.size()) {
                        std::memset(data.data() + at, color, static_cast<size_t>(span));
                    }
                }
            }
        }

        void processNode(const UModel& model, int32_t iNode, const shadow::Projection& projection, const Vector& eye, std::vector<shadow::Polygon>& out)
        {
            for (int32_t n = iNode; n >= 0 && static_cast<size_t>(n) < model.nodes.size(); n = model.nodes[static_cast<size_t>(n)].iPlane) {
                const BspNode& node = model.nodes[static_cast<size_t>(n)];
                float distance = planeDot(node.plane, projection.origin);
                bool front = distance > 0;
                float facing = front ? 1.0f : -1.0f;
                float toward = node.plane.x * projection.zAxis.x + node.plane.y * projection.zAxis.y + node.plane.z * projection.zAxis.z;
                if (toward * facing > 0) {
                    return;
                }
                if (std::abs(distance) > 450.0f) {
                    return;
                }
                if (node.surf < 0 || static_cast<size_t>(node.surf) >= model.surfs.size()) {
                    continue;
                }
                uint32_t flags = model.surfs[static_cast<size_t>(node.surf)].polyFlags;
                if (!front && distance < -1.0f && !(flags & 0x4000100)) {
                    continue;
                }
                if ((flags & 0xc7) || (flags & 0x4000000)) {
                    continue;
                }
                if (std::abs(distance) < 50.0f) {
                    continue;
                }
                std::vector<Vector> points;
                Vector lift { node.plane.x * 2.0f, node.plane.y * 2.0f, node.plane.z * 2.0f };
                for (int i = 0; i < node.numVertices; i++) {
                    size_t vert = static_cast<size_t>(node.vertPool + i);
                    if (vert >= model.verts.size()) {
                        break;
                    }
                    size_t point = static_cast<size_t>(model.verts[vert].vertex);
                    if (point < model.points.size()) {
                        points.push_back(model.points[point] + lift);
                    }
                }
                if (points.size() < 3) {
                    continue;
                }
                std::vector<Vector> clipped;
                if (clipToShadow(projection, points.data(), points.size(), clipped) == 0) {
                    continue;
                }
                if (!front && (flags & 0x4000100)) {
                    std::reverse(clipped.begin(), clipped.end());
                }
                float eyeDistance = planeDot(node.plane, eye);
                if (!(eyeDistance > 0) && eyeDistance < -1.0f) {
                    continue;
                }
                shadow::Polygon polygon;
                for (const Vector& point : clipped) {
                    polygon.u.push_back((point.x + 64.0f) * projection.uScale);
                    polygon.v.push_back((point.y + 64.0f) * projection.vScale);
                    polygon.points.push_back(fromShadow(projection, point));
                }
                out.push_back(std::move(polygon));
            }
        }

        void visit(const UModel& model, int32_t iNode, const shadow::Projection& projection, const Vector& eye, std::vector<shadow::Polygon>& out)
        {
            while (iNode >= 0 && static_cast<size_t>(iNode) < model.nodes.size()) {
                const BspNode& node = model.nodes[static_cast<size_t>(iNode)];
                bool front = planeDot(node.plane, projection.origin) > 0;
                visit(model, front ? node.front : node.back, projection, eye, out);
                processNode(model, iNode, projection, eye, out);
                iNode = front ? node.back : node.front;
            }
        }
    }

    namespace shadow {
        void updateVector(UObject* actor, const std::vector<PreparedLight>& lights)
        {
            Vector location = actor::Location(actor);
            Vector sum;
            int32_t count = 0;
            for (const PreparedLight& light : lights) {
                Vector direction = (light.location - location).normal();
                float weight = static_cast<float>((1.0 / static_cast<double>(light.radius)) * 130.0);
                Vector contribution = direction * weight;
                sum += contribution;
                sum += contribution;
                sum += contribution;
                count += 3;
            }
            if (count) {
                sum = sum * (1.0f / static_cast<float>(count));
            }
            if (sum.x == 0 && sum.y == 0 && sum.z == 0) {
                sum = { 0, 0, 130.0f };
            }
            Vector target = location + sum * 130.0f;
            if (location.z + 5.0f > target.z) {
                target.z = location.z + 5.0f;
            }
            Vector& shadowVector = actor::ShadowVector(actor);
            Vector previous = location - shadowVector;
            Vector source = previous + (target - previous) * 0.5f;
            shadowVector = (location - source).normal() * 130.0f;
        }

        UTexture* texture(UObject* actor)
        {
            UTexture* existing = cast<UTexture>(actor::ShadowTexture(actor));
            if (existing && !existing->mips.empty()) {
                return existing;
            }
            UObject* transient = objects::packageObject(Name("Transient"));
            UTexture* created = cast<UTexture>(objects::newObject(objects::findClass("Engine.ShadowTexture"), transient, Name(), 0));
            UPalette* palette = cast<UPalette>(objects::newObject(objects::findClass("Engine.Palette"), transient, Name(), 0));
            if (!created || !palette) {
                return nullptr;
            }
            palette->colors.resize(256);
            for (uint32_t i = 0; i < 256; i++) {
                palette->colors[i] = i | (i << 8) | (i << 16) | 0xff000000u;
            }
            palette->colors[0] = 0;
            bitmap::Palette(created) = palette;
            bitmap::USize(created) = TextureSize;
            bitmap::VSize(created) = TextureSize;
            bitmap::UClamp(created) = TextureSize;
            bitmap::VClamp(created) = TextureSize;
            bitmap::UBits(created) = 7;
            bitmap::VBits(created) = 7;
            TextureMip mip;
            mip.width = TextureSize;
            mip.height = TextureSize;
            mip.ubits = 7;
            mip.vbits = 7;
            mip.data.assign(static_cast<size_t>(TextureSize * TextureSize), 0);
            created->mips.push_back(std::move(mip));
            actor::ShadowTexture(actor) = created;
            return created;
        }

        Projection projection(UObject* actor, UTexture* texture)
        {
            Coords axes = rotationAxes(unrealRotation(actor::ShadowVector(actor)));
            Projection result;
            result.origin = actor::Location(actor) + actor::PrePivot(actor) - actor::ShadowVector(actor);
            result.xAxis = axes.yAxis;
            result.yAxis = axes.zAxis * -1.0f;
            result.zAxis = axes.xAxis;
            result.uScale = static_cast<float>(static_cast<double>(bitmap::USize(texture)) * 0.0078125);
            result.vScale = static_cast<float>(static_cast<double>(bitmap::VSize(texture)) * 0.0078125);
            return result;
        }

        void clear(UTexture* texture)
        {
            if (!texture->mips.empty()) {
                std::fill(texture->mips[0].data.begin(), texture->mips[0].data.end(), static_cast<uint8_t>(128));
            }
        }

        void drawTriangle(UTexture* texture, const Projection& projection, const Vector* triangle)
        {
            Vector normal = (triangle[0] - triangle[1]).cross(triangle[2] - triangle[0]);
            float w = normal.dot(triangle[0]);
            if (!(normal.dot(projection.origin) - w < 0.0f)) {
                return;
            }
            std::vector<Vector> clipped;
            int count = clipToShadow(projection, triangle, 3, clipped);
            if (count == 0 || count > 16) {
                return;
            }
            std::vector<Vector> points(static_cast<size_t>(count));
            for (int i = 0; i < count; i++) {
                points[static_cast<size_t>(i)] = { (clipped[static_cast<size_t>(i)].x + 64.0f) * projection.uScale, (clipped[static_cast<size_t>(i)].y + 64.0f) * projection.vScale, 0 };
            }
            drawPolygon(texture, points.data(), count);
        }

        void receivers(const UModel& model, const Projection& projection, const Vector& eye, std::vector<Polygon>& out)
        {
            if (model.nodes.empty()) {
                return;
            }
            visit(model, 0, projection, eye, out);
        }
    }
}
