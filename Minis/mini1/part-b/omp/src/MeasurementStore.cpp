#include "MeasurementStore.hpp"

#include "AosStore.hpp"

namespace aq {

ValueRangePredicate::ValueRangePredicate(const ValueQuery& query) : query_(query) {}

bool ValueRangePredicate::matches(const Measurement& row) const {
    return row.value >= query_.lo && row.value <= query_.hi && query_.range.contains(row.hour);
}

std::optional<StoreKind> storeKindFromName(std::string_view name) {
    if (name == "aos") return StoreKind::Aos;
    return std::nullopt;
}

std::string_view storeKindName(StoreKind kind) {
    switch (kind) {
        case StoreKind::Aos:
            return "aos";
    }
    return "unknown";
}

std::unique_ptr<MeasurementStore> makeStore(StoreKind kind) {
    switch (kind) {
        case StoreKind::Aos:
            return std::make_unique<AosStore>();
    }
    return nullptr;
}

}  // namespace aq
