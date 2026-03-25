# Project Notes

## Font Sizes

Draxul renders fonts at the display's true physical DPI (e.g. 220 PPI on Retina). When comparing or choosing font sizes, use 72 DPI as the baseline — a font that looks right at 11pt in Draxul may need to be set to ~15pt in Terminal.app or other apps that assume 72 DPI.

## Bold/Italic Font Variant Resolution

The current approach (filename substitution: `-Regular` → `-Bold`, `-Italic`, `-BoldItalic`) works for bundled fonts with predictable naming conventions but breaks for arbitrary system fonts.

When a system font picker is added, variant resolution should use OS APIs:

- **macOS**: CoreText — `CTFontCreateCopyWithSymbolicTraits` with `kCTFontBoldTrait` / `kCTFontItalicTrait`, then extract the file URL via `CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute)`
- **Windows**: DirectWrite — `IDWriteFontFamily::GetFirstMatchingFont` with `DWRITE_FONT_WEIGHT_BOLD` / `DWRITE_FONT_STYLE_ITALIC`
- **Linux**: fontconfig — `FcPatternBuild` with `FC_WEIGHT` / `FC_SLANT`, then `FcFontMatch`

The correct layered resolution order is:
1. Explicit config (`bold_font_path` etc.) — user override always wins
2. OS font API — query the same family for the matching weight/style
3. FreeType family enumeration — scan faces in the same family by `style_flags`
4. Filename substitution — current fallback, bundled fonts only

This work belongs in a new `platform_font_resolver` layer, and should be implemented at the same time as any system font picker UI.
