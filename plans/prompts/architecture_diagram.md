# Architecture Diagram Prompt

Generate an SVG architecture diagram of the application showing how classes are inherited and linked.

## Instructions

Ignoring tests, draw an SVG diagram of this application, showing how things are inherited/linked. Make it as clear as possible so I can easily see the structure of how it fits together.

## Expected Output

- A single SVG file written to `docs/architecture.svg`
- Color-coded boxes: interfaces (blue), abstract bases (yellow), concrete classes (green), app orchestrators (red), support/pipeline (purple)
- Hollow arrows for inheritance, dashed filled arrows for composition/ownership
- Sections: Renderer hierarchy, Host hierarchy, Window, Font & Grid, App layer
- Data flow diagram at the bottom showing both nvim and terminal paths
- Threading model note
