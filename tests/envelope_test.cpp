// Build & run:  clang++ -std=c++17 tests/envelope_test.cpp -o /tmp/envtest && /tmp/envtest
#include "../Source/Envelope.h"
#include <cassert>
#include <cstdio>
#include <vector>

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

int main()
{
    Envelope e;
    assert(near(e.evaluate(0.f), 0.f));          // start of pump is fully "ducked"
    assert(near(e.evaluate(0.6f), 1.f));
    assert(near(e.evaluate(0.9f), 1.f));
    assert(near(e.evaluate(1.25f), e.evaluate(0.25f))); // wraps

    // Linear segment is linear
    e.points = { { 0.f, 0.f, 0.f }, { 1.f, 1.f, 0.f } };
    assert(near(e.evaluate(0.5f), 0.5f));

    // Positive curve = fast start
    e.points[0].curve = 0.5f;
    assert(e.evaluate(0.25f) > 0.25f);
    e.points[0].curve = -0.5f;
    assert(e.evaluate(0.25f) < 0.25f);

    // Bake table
    std::vector<float> table(Envelope::tableSize + 1);
    e.points[0].curve = 0.f;
    e.bake(table.data());
    assert(near(table[0], 0.f));
    assert(near(table[Envelope::tableSize / 2], 0.5f));
    assert(near(table[Envelope::tableSize], 1.f, 1e-3f));

    // Serialize round trip + normalize
    Envelope p;
    p.points = { { 0.1f, 0.2f, 0.3f }, { 0.5f, 1.f, 0.f }, { 0.9f, 0.f, -0.2f } };
    Envelope q = Envelope::deserialize(p.serialize());
    assert(q.points.size() == 3);
    assert(near(q.points.front().x, 0.f) && near(q.points.back().x, 1.f));
    assert(near(q.points[1].y, 1.f));

    std::puts("envelope tests passed");
    return 0;
}
