// engine/renderer/stb_image_impl.cpp
//
// Single compilation unit for stb_image and stb_image_write. Both libraries
// use the *IMPLEMENTATION macro pattern which must be defined in exactly one
// translation unit. Putting them here avoids duplicate-symbol linker errors
// (LNK4006) when both renderer.cpp and software_renderer.cpp include the
// headers.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
