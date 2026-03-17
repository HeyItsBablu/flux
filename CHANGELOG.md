# Changelog

All notable changes to FluxUI will be documented here.

## [Unreleased]
### Added
- Nothing yet

---

## [0.1.0] - 2026-03-17

### Added
- Initial public release
- Core widget system (`Component`, `State`, `WidgetPtr`)
- Layout widgets: `Row`, `Column`, `Expanded`, `Scaffold`, `Center`, `SizedBox`, `Divider`
- Display widgets: `Text`, `Container`, `AppBar`
- Input widgets: `Button`, `TextInput`
- Collection widgets: `GridView`, `ListView`
- Reactive state system with `State<T>`
- Light and dark theme support via `AppTheme`
- OpenGL/GDI+ renderer backend
- FetchContent support for easy integration
- CMake install rules with `flux::flux` namespaced target
- Built-in examples: counter, draggable, layout, listview, graphtest,
  illustrator, image_editor, overlaytest, paintfull, logic_sim