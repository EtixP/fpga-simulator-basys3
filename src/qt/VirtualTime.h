#pragma once

#include <QString>

#include <cstdint>

namespace vb::qt {

// Exact integer formatting of virtual time: one cycle is 10 ns. Divide first,
// because cycle*10 would overflow long before the cycle counter does.
inline QString formatVirtualTime(uint64_t cycle) {
    return QStringLiteral("%1.%2 s")
        .arg(cycle / 100'000'000)
        .arg((cycle % 100'000'000) * 10, 9, 10, QLatin1Char('0'));
}

} // namespace vb::qt
