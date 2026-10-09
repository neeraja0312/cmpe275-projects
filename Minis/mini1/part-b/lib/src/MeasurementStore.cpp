#include "MeasurementStore.hpp"

#include "AosStore.hpp"
#include "ColumnStore.hpp"

namespace aq {

ValueRangePredicate::ValueRangePredicate(const ValueQuery& query) : query_(query) {}

bool ValueRangePredicate::matches(const Measurement& row) const {
    return row.value >= query_.lo && row.value <= query_.hi && query_.range.contains(row.hour);
}

std::optional<Layout> layoutFromName(std::string_view name) {
    if (name == "aos") return Layout::Aos;
    if (name == "columns" || name == "soa") return Layout::Columns;
    return std::nullopt;
}

std::string_view layoutName(Layout layout) {
    switch (layout) {
        case Layout::Aos:
            return "aos";
        case Layout::Columns:
            return "columns";
    }
    return "unknown";
}

std::unique_ptr<MeasurementStore> makeStore(Layout layout) {
    switch (layout) {
        case Layout::Aos:
            return std::make_unique<AosStore>();
        case Layout::Columns:
            return std::make_unique<ColumnStore>();
    }
    return nullptr;
}

}  // namespace aq
