#define INCBIN_PREFIX g_
#include "incbin.h"

INCBIN(FluxTemplate, FLUX_TEMPLATE_BIN_PATH);

extern const unsigned char* flux_template_data() { return g_FluxTemplateData; }
extern size_t flux_template_size() { return g_FluxTemplateSize; }