/*
 * Copyright © 2020 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "freedreno_dev_info.h"
#include "freedreno_uuid.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "util/mesa-blake3.h"
#include "git_sha1.h"

/* (Re)define UUID_SIZE to avoid including vulkan.h (or p_defines.h) here. */
#define UUID_SIZE 16

void
fd_get_driver_uuid(void *uuid)
{
   const char *driver_id = PACKAGE_VERSION MESA_GIT_SHA1;

   /* The driver UUID is used for determining sharability of images and memory
    * between two Vulkan instances in separate processes, but also to
    * determining memory objects and sharability between Vulkan and OpenGL
    * driver. People who want to share memory need to also check the device
    * UUID.
    */
   blake3_hasher blake3_ctx;
   _mesa_blake3_init(&blake3_ctx);

   _mesa_blake3_update(&blake3_ctx, driver_id, strlen(driver_id));

   uint8_t blake3[BLAKE3_KEY_LEN];
   _mesa_blake3_final(&blake3_ctx, blake3);

   assert(BLAKE3_KEY_LEN >= UUID_SIZE);
   memcpy(uuid, blake3, UUID_SIZE);
}

void
fd_get_device_uuid(void *uuid, const struct fd_dev_id *id)
{
   blake3_hasher blake3_ctx;
   _mesa_blake3_init(&blake3_ctx);

   /* The device UUID uniquely identifies the given device within the machine.
    * Since we never have more than one device, this doesn't need to be a real
    * UUID, so we use BLAKE3("freedreno" + gpu_id).
    *
    * @TODO: Using the GPU id could be too restrictive on the off-chance that
    * someone would like to use this UUID to cache pre-tiled images or something
    * of the like, and use them across devices. In the future, we could allow
    * that by:
    * * Being a bit loose about GPU id and hash only the generation's
    * 'major' number (e.g, '6' instead of '630').
    *
    * * Include HW specific constants that are relevant for layout resolving,
    * like minimum width to enable UBWC, tile_align_w, etc.
    *
    * This would allow cached device memory to be safely used from HW in
    * (slightly) different revisions of the same generation.
    */

   static const char *device_name = "freedreno";
   _mesa_blake3_update(&blake3_ctx, device_name, strlen(device_name));

   _mesa_blake3_update(&blake3_ctx, id, sizeof(*id));

   uint8_t blake3[BLAKE3_KEY_LEN];
   _mesa_blake3_final(&blake3_ctx, blake3);

   assert(BLAKE3_KEY_LEN >= UUID_SIZE);
   memcpy(uuid, blake3, UUID_SIZE);
}
