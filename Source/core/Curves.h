#pragma once

// Curve shapes shared by the model and the engine (PLUGIN-PLAN 1.2).

namespace sl::core
{
    /** PulseLock's bend curve (Pattern.cpp bendShape): bend > 0 starts slow and finishes fast, bend < 0 the
        reverse, |bend| < 1e-3 is straight. (exp (8 bend u) - 1) / (exp (8 bend) - 1), with V8's exp (JsMath.h). */
    double bendShape (double u, double bend) noexcept;

    /** v3's glide curve: smoothstep, bent towards an early (c > 0) or late (c < 0) arrival. u is clamped to 0..1. */
    double gShape (double u, double c) noexcept;
}
