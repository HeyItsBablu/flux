#ifndef FLUX_TEMPLATE_RESOURCE_H
#define FLUX_TEMPLATE_RESOURCE_H


// Keep this numeric value in sync with the hardcoded "101" in the
// generated flux_template.rc (cli/CMakeLists.txt) — rc.exe's preprocessor
// doesn't reliably #include custom headers, so the .rc uses the literal.
#define IDR_FLUX_TEMPLATE 101

#endif