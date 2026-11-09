#pragma once

// JavaScript's own maths functions where the C library's give a different last bit (PLUGIN-PLAN 1.6 rule 9).
// V8's Math.exp is fdlibm's e_exp.c (V8 src/base/ieee754.cc), plus one special case: exp (1) is Math.E.
// std::exp may round the other way about once in a few hundred inputs, and bendShape's (exp (k u) - 1)
// turns that one bit into up to 5e-15 relative, more than model/seeds.json allows (4e-16). So bendShape
// uses this port, on every platform. It was checked against node 24's Math.exp on 2.2 million inputs:
// all bit for bit.

namespace sl::core
{
    /** Math.exp, bit for bit as V8 computes it. */
    double jsExp (double x) noexcept;
}
