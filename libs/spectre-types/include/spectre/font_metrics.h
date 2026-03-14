#pragma once

namespace spectre {

struct FontMetrics {
    int cell_width;    // Monospace advance width
    int cell_height;   // Line height
    int ascender;      // Pixels above baseline
    int descender;     // Pixels below baseline (positive = down)
};

} // namespace spectre
