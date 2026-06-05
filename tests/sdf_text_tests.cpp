#include "undecedent/sdf_text.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "SDF text test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    require(
        undecedent::load_sdf_text_font("assets/fonts/google_sans_code_msdf.fnt"),
        "MSDF font metrics should load"
    );
    require(undecedent::sdf_text_ready(), "MSDF text metrics should be ready");

    const undecedent::SdfTextMetrics lower = undecedent::measure_sdf_text("select point_light.radius", 16.0F);
    require(lower.width > 0.0F, "lowercase and punctuation text should measure");
    require(lower.height > 0.0F, "single-line text should have height");
    require(lower.lines == 1, "single-line text should report one line");

    const undecedent::SdfTextMetrics multiline = undecedent::measure_sdf_text("if true\n    print(1)", 16.0F);
    require(multiline.lines == 2, "newline should report two lines");
    require(multiline.height > lower.height, "multiline text should be taller than one line");

    const undecedent::SdfTextMetrics spaces = undecedent::measure_sdf_text("    ", 16.0F);
    const undecedent::SdfTextMetrics tab = undecedent::measure_sdf_text("\t", 16.0F);
    require(tab.width >= spaces.width * 0.95F, "tab should measure close to four spaces");

    require(
        !undecedent::load_sdf_text_font("assets/fonts/does_not_exist.fnt"),
        "missing MSDF metrics should fail cleanly"
    );

    std::cout << "SDF text tests passed.\n";
    return EXIT_SUCCESS;
}
