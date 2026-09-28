#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Passcode + bearer-token auth backing the dashboard's lock screen.
 * Not part of the original firmware spec — added so the dashboard
 * (which already implements this exact contract) has something to
 * authenticate against:
 *
 *   POST /api/auth  {"code":"4712"}  -> 200 {"token":"..."} | 401
 *   Authorization: Bearer <token>    on every protected endpoint
 *
 * Tokens are opaque, random, and expire after AUTH_TOKEN_TTL_MS. This is
 * appropriate for a LAN-only home device; it is not hardened against a
 * determined attacker who already has network access to the box.
 */

#define AUTH_TOKEN_LEN      32   /* hex chars, not counting nul */
#define AUTH_MAX_TOKENS      4
#define AUTH_TOKEN_TTL_MS   (24ULL * 60 * 60 * 1000)

esp_err_t auth_init(void);

/* Compares against the stored passcode (NVS key "auth_code", default
 * "4712"). On match, fills token_out (AUTH_TOKEN_LEN+1 bytes) with a new
 * token and returns true. */
bool auth_check_code(const char *code, char *token_out);

/* Validates a bearer token against the live token table (checking
 * expiry). */
bool auth_check_token(const char *token);
