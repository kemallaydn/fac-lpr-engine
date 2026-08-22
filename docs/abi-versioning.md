# Semantic versioning and C ABI compatibility

FAC LPR Engine versions the product with Semantic Versioning (`MAJOR.MINOR.PATCH`) and versions the public C ABI independently with an ABI major.

Current product version: `0.1.0`.
Current public C ABI major: `1`.

## Compatibility rules

### Patch releases

Patch releases may fix bugs, improve performance, or change implementation details. They must not remove public v1 symbols, change existing enum/status numeric values, reorder or shrink published v1 structs, change calling conventions, or change ownership/lifetime rules.

### Minor releases

Minor releases may add functionality while retaining binary compatibility with the current ABI major. New functions must use new symbols. Existing versioned structs may only evolve through an explicitly compatible append-only strategy where callers communicate `struct_size`; existing fields, offsets and meanings remain stable. Reserved fields must remain accepted according to the documented contract.

### Major / breaking ABI changes

Any change that can break an already compiled consumer requires an ABI-major bump. Examples include removing or renaming a public symbol, changing a function signature/calling convention, changing an existing field type/offset/meaning, changing enum/status numeric values, or changing caller/engine ownership rules.

A new ABI major uses new versioned public symbols and structs such as `*_v2`; v1 symbols are not silently repurposed. Product SemVer major must also be bumped for a stable public release when compatibility promised by the current product major is broken.

## Runtime version query

Consumers call `fac_lpr_get_version_v1()` with `FAC_LPR_VERSION_INFO_V1_INIT`. The returned record exposes semantic major/minor/patch and ABI major. Invalid `struct_size` or ABI version is rejected rather than guessed.

## Deprecation

A public ABI symbol is deprecated before removal. Deprecation must be documented in release notes and provide the replacement path. Removal is only allowed in a breaking ABI-major release. Internal C++ APIs are not covered by the binary compatibility promise unless explicitly published as part of the C ABI.

## Compatibility tests

The native test suite locks the v1 runtime version query, C-header compilation, struct-size/version validation, status behavior and result-buffer wire layout. Release CI must build against the current public header and preserve these tests. Future compatibility validation may additionally compile a saved previous-version consumer against a new binary; such a test supplements, rather than replaces, the v1 wire-layout regression checks.
