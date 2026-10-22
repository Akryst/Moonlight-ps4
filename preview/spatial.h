#pragma once
#include "cross2d/c2d.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace spatial {
struct Point { float x, y, z; };
inline Point rotate(Point p, float angle) {
    float x = p.x * std::cos(angle) + p.z * std::sin(angle);
    float z = -p.x * std::sin(angle) + p.z * std::cos(angle);
    return {x, p.y * 0.91f - z * 0.41f, p.y * 0.41f + z * 0.91f};
}
inline c2d::Vector2f project(Point p, float focal = 210) {
    float scale = focal / (4.5f - p.z);
    return {p.x * scale, p.y * scale};
}

// A rotating icosahedron and two spatial rings, projected into libcross2d's triangle batch.
class Crystal : public c2d::Rectangle {
public:
    Crystal() : c2d::Rectangle(c2d::Vector2f(0, 0)), faces(c2d::Triangles, 60), rings(c2d::Lines, 256) { rebuild(); }
    bool reduced_motion = false;
    float angle = 0.5f;
protected:
    void onUpdate() override {
        if (!isVisible()) return;
        if (!reduced_motion) angle += std::min(0.05f, c2d_renderer->getDeltaTime().asSeconds()) * 0.23f;
        rebuild();
    }
    void onDraw(c2d::Transform &transform, bool draw) override {
        if (!draw || !isVisible()) return;
        auto combined = transform * getTransform();
        c2d_renderer->draw(&rings, combined, nullptr);
        c2d_renderer->draw(&faces, combined, nullptr);
    }
private:
    c2d::VertexArray faces, rings;
    void rebuild() {
        constexpr float g = 1.618034f;
        const std::array<Point, 12> source = {{{-1,g,0},{1,g,0},{-1,-g,0},{1,-g,0},
            {0,-1,g},{0,1,g},{0,-1,-g},{0,1,-g},{g,0,-1},{g,0,1},{-g,0,-1},{-g,0,1}}};
        constexpr int indices[20][3] = {{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
            {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},{3,9,4},{3,4,2},
            {3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
        std::array<Point, 12> points;
        for (int i = 0; i < 12; ++i) points[i] = spatial::rotate(source[i], angle);
        std::array<int, 20> order;
        for (int i = 0; i < 20; ++i) order[i] = i;
        auto depth = [&](int i) { return points[indices[i][0]].z + points[indices[i][1]].z + points[indices[i][2]].z; };
        std::sort(order.begin(), order.end(), [&](int a, int b) { return depth(a) < depth(b); });
        for (int i = 0; i < 20; ++i) {
            int face = order[i];
            auto a = points[indices[face][0]], b = points[indices[face][1]], c = points[indices[face][2]];
            Point u{b.x-a.x,b.y-a.y,b.z-a.z}, v{c.x-a.x,c.y-a.y,c.z-a.z};
            Point n{u.y*v.z-u.z*v.y, u.z*v.x-u.x*v.z, u.x*v.y-u.y*v.x};
            float light = std::clamp((n.x * -0.3f + n.y * -0.6f + n.z * 0.74f) /
                std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z), -1.0f, 1.0f) * 0.5f + 0.5f;
            c2d::Color color(28 + light * 104, 55 + light * 148, 106 + light * 143);
            for (int j = 0; j < 3; ++j) faces[i * 3 + j] = c2d::Vertex(project(points[indices[face][j]]), color);
        }
        for (int ring = 0; ring < 2; ++ring) for (int i = 0; i < 64; ++i) {
            for (int end = 0; end < 2; ++end) {
                float a = (i + end) * 6.2831853f / 64;
                Point p{2.75f * std::cos(a), 0.35f * std::sin(a), 2.75f * std::sin(a)};
                if (ring) std::swap(p.x, p.y);
                auto rotated = spatial::rotate(p, -angle * 0.4f);
                rings[ring * 128 + i * 2 + end] = c2d::Vertex(project(rotated),
                    ring ? c2d::Color(111, 117, 214, 95) : c2d::Color(75, 207, 233, 130));
            }
        }
        faces.update(); rings.update();
    }
};

enum class Button { Cross, Circle, Square, Triangle, Options, Dpad, L1, R1 };
}
