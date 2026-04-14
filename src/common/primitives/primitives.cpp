#include "common/primitives/primitives.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace lu::assets {

PrimitiveMesh generate_box(float cx, float cy, float cz,
                            float hx, float hy, float hz) {
    PrimitiveMesh m;
    for (int i = 0; i < 8; ++i) {
        float x = cx + ((i & 1) ? hx : -hx);
        float y = cy + ((i & 2) ? hy : -hy);
        float z = cz + ((i & 4) ? hz : -hz);
        m.vertices.push_back(x); m.vertices.push_back(y); m.vertices.push_back(z);
    }
    static const uint32_t faces[36] = {
        0,2,1,1,2,3, 4,5,6,5,7,6,
        0,1,4,1,5,4, 2,6,3,3,6,7,
        0,4,2,2,4,6, 1,3,5,3,7,5,
    };
    m.indices.assign(faces, faces + 36);
    return m;
}

PrimitiveMesh generate_sphere(float cx, float cy, float cz, float radius,
                               int rings, int sectors) {
    PrimitiveMesh m;
    for (int r = 0; r <= rings; ++r) {
        float phi = static_cast<float>(M_PI) * r / rings;
        float sp = std::sin(phi), cp = std::cos(phi);
        for (int s = 0; s <= sectors; ++s) {
            float theta = 2.0f * static_cast<float>(M_PI) * s / sectors;
            m.vertices.push_back(cx + radius * sp * std::cos(theta));
            m.vertices.push_back(cy + radius * cp);
            m.vertices.push_back(cz + radius * sp * std::sin(theta));
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < sectors; ++s) {
            uint32_t a = r * (sectors+1) + s, b = a + 1;
            uint32_t c = (r+1) * (sectors+1) + s, d = c + 1;
            m.indices.push_back(a); m.indices.push_back(c); m.indices.push_back(b);
            m.indices.push_back(b); m.indices.push_back(c); m.indices.push_back(d);
        }
    return m;
}

PrimitiveMesh generate_capsule(const float* a, const float* b, float radius,
                                int rings, int sectors) {
    PrimitiveMesh m;
    float dx = b[0]-a[0], dy = b[1]-a[1], dz = b[2]-a[2];
    float len = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (len < 1e-6f) return generate_sphere(a[0], a[1], a[2], radius, rings, sectors);

    // Build local coordinate frame along capsule axis
    float az[3] = {dx/len, dy/len, dz/len};
    float up[3] = {0, 1, 0};
    if (std::fabs(az[0]*up[0] + az[1]*up[1] + az[2]*up[2]) > 0.99f) { up[0]=1; up[1]=0; up[2]=0; }
    float ax[3] = {az[1]*up[2]-az[2]*up[1], az[2]*up[0]-az[0]*up[2], az[0]*up[1]-az[1]*up[0]};
    float al = std::sqrt(ax[0]*ax[0] + ax[1]*ax[1] + ax[2]*ax[2]);
    ax[0]/=al; ax[1]/=al; ax[2]/=al;
    float ay[3] = {az[1]*ax[2]-az[2]*ax[1], az[2]*ax[0]-az[0]*ax[2], az[0]*ax[1]-az[1]*ax[0]};

    int hr = std::max(rings/2, 2);
    auto addV = [&](float cx, float cy, float cz, float phi, float theta) {
        float sp = std::sin(phi)*radius, cp = std::cos(phi)*radius;
        float st = std::sin(theta), ct = std::cos(theta);
        float lx = sp*ct, ly = sp*st, lz = cp;
        m.vertices.push_back(cx + ax[0]*lx + ay[0]*ly + az[0]*lz);
        m.vertices.push_back(cy + ax[1]*lx + ay[1]*ly + az[1]*lz);
        m.vertices.push_back(cz + ax[2]*lx + ay[2]*ly + az[2]*lz);
    };

    // Top hemisphere (around point b)
    for (int r = 0; r <= hr; r++) {
        float phi = static_cast<float>(M_PI) * 0.5f * r / hr;
        for (int s = 0; s <= sectors; s++)
            addV(b[0], b[1], b[2], phi, 2.0f * static_cast<float>(M_PI) * s / sectors);
    }
    uint32_t bb = static_cast<uint32_t>(m.vertices.size() / 3);

    // Bottom hemisphere (around point a)
    for (int r = 0; r <= hr; r++) {
        float phi = static_cast<float>(M_PI) * 0.5f + static_cast<float>(M_PI) * 0.5f * r / hr;
        for (int s = 0; s <= sectors; s++)
            addV(a[0], a[1], a[2], phi, 2.0f * static_cast<float>(M_PI) * s / sectors);
    }

    int cols = sectors + 1;
    // Top hemisphere triangles
    for (int r = 0; r < hr; r++)
        for (int s = 0; s < sectors; s++) {
            uint32_t va = r*cols+s, vb = va+1, vc = (r+1)*cols+s, vd = vc+1;
            m.indices.push_back(va); m.indices.push_back(vc); m.indices.push_back(vb);
            m.indices.push_back(vb); m.indices.push_back(vc); m.indices.push_back(vd);
        }
    // Cylinder band connecting hemispheres
    uint32_t te = hr * cols, be = bb;
    for (int s = 0; s < sectors; s++) {
        m.indices.push_back(te+s); m.indices.push_back(be+s); m.indices.push_back(te+s+1);
        m.indices.push_back(te+s+1); m.indices.push_back(be+s); m.indices.push_back(be+s+1);
    }
    // Bottom hemisphere triangles
    for (int r = 0; r < hr; r++)
        for (int s = 0; s < sectors; s++) {
            uint32_t va = bb+r*cols+s, vb = va+1, vc = bb+(r+1)*cols+s, vd = vc+1;
            m.indices.push_back(va); m.indices.push_back(vc); m.indices.push_back(vb);
            m.indices.push_back(vb); m.indices.push_back(vc); m.indices.push_back(vd);
        }
    return m;
}

PrimitiveMesh generate_cylinder(const float* a, const float* b, float radius,
                                 int segments) {
    PrimitiveMesh m;
    float dx = b[0]-a[0], dy = b[1]-a[1], dz = b[2]-a[2];
    float len = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (len < 1e-6f) return m;

    float nx = dx/len, ny = dy/len, nz = dz/len;
    // Build perpendicular axes
    float ux, uy, uz;
    if (std::abs(nx) < 0.9f) { ux=0; uy=-nz; uz=ny; }
    else { ux=-nz; uy=0; uz=nx; }
    float ulen = std::sqrt(ux*ux + uy*uy + uz*uz);
    ux/=ulen; uy/=ulen; uz/=ulen;
    float vx = ny*uz - nz*uy, vy = nz*ux - nx*uz, vz = nx*uy - ny*ux;

    // Bottom ring
    for (int i = 0; i < segments; ++i) {
        float angle = 2.0f * static_cast<float>(M_PI) * i / segments;
        float c = std::cos(angle)*radius, s = std::sin(angle)*radius;
        float px = ux*c + vx*s, py = uy*c + vy*s, pz = uz*c + vz*s;
        m.vertices.push_back(a[0]+px); m.vertices.push_back(a[1]+py); m.vertices.push_back(a[2]+pz);
    }
    // Top ring
    for (int i = 0; i < segments; ++i) {
        float angle = 2.0f * static_cast<float>(M_PI) * i / segments;
        float c = std::cos(angle)*radius, s = std::sin(angle)*radius;
        float px = ux*c + vx*s, py = uy*c + vy*s, pz = uz*c + vz*s;
        m.vertices.push_back(b[0]+px); m.vertices.push_back(b[1]+py); m.vertices.push_back(b[2]+pz);
    }
    // Center points for caps
    m.vertices.push_back(a[0]); m.vertices.push_back(a[1]); m.vertices.push_back(a[2]);
    m.vertices.push_back(b[0]); m.vertices.push_back(b[1]); m.vertices.push_back(b[2]);
    int ca = segments*2, cb = ca+1;

    // Side quads
    for (int i = 0; i < segments; ++i) {
        int n = (i+1) % segments;
        m.indices.push_back(i); m.indices.push_back(n); m.indices.push_back(segments+i);
        m.indices.push_back(segments+i); m.indices.push_back(n); m.indices.push_back(segments+n);
    }
    // Bottom cap
    for (int i = 0; i < segments; ++i) {
        int n = (i+1) % segments;
        m.indices.push_back(ca); m.indices.push_back(n); m.indices.push_back(i);
    }
    // Top cap
    for (int i = 0; i < segments; ++i) {
        int n = (i+1) % segments;
        m.indices.push_back(cb); m.indices.push_back(segments+i); m.indices.push_back(segments+n);
    }
    return m;
}

} // namespace lu::assets
