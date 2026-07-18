/*
 * ReactOS glue for the standalone vkd3d-shader subset.
 *
 * Wine links vkd3d-shader into the complete vkd3d runtime, whose main module
 * supplies this debug channel name. ReactOS currently builds only the shader
 * library, so provide the corresponding shader channel at that boundary.
 */

#include "vkd3d_common.h"

VKD3D_DEBUG_ENV_NAME("VKD3D_SHADER_DEBUG");
