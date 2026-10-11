#pragma once

// Zero-copy view of one monitor's rows, independent of the storage layout.
//
// A column slice does not say how the library keeps its data. For the
// array-of-structs layout each column is read with a stride of 12 bytes (one
// Measurement); for the column layout the stride is the element size. Either
// way the caller reads hour(i), value(i), ... and nothing is copied.

#include "aqdata/Types.hpp"

#include <cstddef>
#include <cstring>

namespace aq {

template <typename T>
class StridedColumn {
public:
    StridedColumn() = default;
    StridedColumn(const T* first, std::size_t strideBytes)
        : base_(reinterpret_cast<const std::byte*>(first)), stride_(strideBytes) {}

    T operator[](std::size_t i) const {
        T value;
        std::memcpy(&value, base_ + i * stride_, sizeof(T));
        return value;
    }

private:
    const std::byte* base_ = nullptr;
    std::size_t stride_ = 0;
};

struct ColumnSlice {
    MonitorId monitor = 0;
    std::size_t size = 0;
    StridedColumn<HourIndex> hour;
    StridedColumn<ScaledValue> value;
    StridedColumn<CodeId> qualifier;
    StridedColumn<CodeId> method;

    Measurement row(std::size_t i) const {
        return Measurement{hour[i], monitor, value[i], qualifier[i], method[i]};
    }
};

}  // namespace aq
