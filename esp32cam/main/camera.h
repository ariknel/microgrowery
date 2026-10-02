#pragma once

#include "esp_err.h"
#include "esp_camera.h"

esp_err_t camera_init(void);

/* Thin wrappers around esp_camera_fb_get/return — kept so callers only
 * depend on this header, not esp_camera.h directly. */
camera_fb_t *camera_capture(void);
void camera_release(camera_fb_t *fb);
