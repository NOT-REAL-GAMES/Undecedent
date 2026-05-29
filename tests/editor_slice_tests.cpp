#include "undecedent/editor_slice.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    undecedent::SectorPlane sector;
    sector.floor_height = 32.0F;
    sector.height = 96.0F;

    expect(!undecedent::sector_intersects_z_slice(sector, 24.0F), "slice below floor should not intersect sector");
    expect(undecedent::sector_intersects_z_slice(sector, 32.0F), "slice at floor should intersect sector");
    expect(undecedent::sector_intersects_z_slice(sector, 96.0F), "slice inside sector should intersect sector");
    expect(undecedent::sector_intersects_z_slice(sector, 128.0F), "slice at ceiling should intersect sector");
    expect(!undecedent::sector_intersects_z_slice(sector, 136.0F), "slice above ceiling should not intersect sector");
    expect(undecedent::sector_floor_matches_slice(sector, 32.0F), "floor slice should match active floor");
    expect(!undecedent::sector_floor_matches_slice(sector, 96.0F), "interior slice should not match active floor");

    return EXIT_SUCCESS;
}
