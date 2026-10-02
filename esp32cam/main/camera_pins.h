#pragma once

/* AI-Thinker ESP32-CAM pin mapping — the standard layout used by the
 * common AI-Thinker board and most compatible clones. If frames come back
 * garbled or esp_camera_init() fails, double-check these against your
 * specific board's silkscreen; cheap clones occasionally differ. */

#define CAM_PIN_PWDN   32
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK    0
#define CAM_PIN_SIOD   26
#define CAM_PIN_SIOC   27
#define CAM_PIN_D7     35
#define CAM_PIN_D6     34
#define CAM_PIN_D5     39
#define CAM_PIN_D4     36
#define CAM_PIN_D3     21
#define CAM_PIN_D2     19
#define CAM_PIN_D1     18
#define CAM_PIN_D0      5
#define CAM_PIN_VSYNC  25
#define CAM_PIN_HREF   23
#define CAM_PIN_PCLK   22
