// stb_image implementation TU. Kept separate so the (large) decoder lives in
// its own translation unit and launcher.cpp only sees the declarations.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO          // we feed it memory buffers via RmlUi's FileInterface
#define STBI_ONLY_PNG          // launcher art is PNG; keeps the decoder small
#include "third_party/stb_image.h"
