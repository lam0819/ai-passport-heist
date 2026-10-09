#pragma once
// Host stand-in for NVS: a small in-memory key/value table, so the harness
// can check that progress survives a "reboot" (settings_load() again).
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
typedef int nvs_handle_t;
#define NVS_READWRITE 1
#define ESP_ERR_NVS_NOT_FOUND 0x1102

typedef struct { char key[16]; uint8_t val[64]; size_t len; int used; } stub_nvs_ent_t;
static stub_nvs_ent_t g_stub_nvs[32];
static int g_stub_nvs_commits;

static inline stub_nvs_ent_t *stub_nvs_find(const char *k, int create)
{
    for (int i = 0; i < 32; i++) if (g_stub_nvs[i].used && !strcmp(g_stub_nvs[i].key, k)) return &g_stub_nvs[i];
    if (!create) return NULL;
    for (int i = 0; i < 32; i++) if (!g_stub_nvs[i].used) {
        g_stub_nvs[i].used = 1;
        strncpy(g_stub_nvs[i].key, k, 15);
        return &g_stub_nvs[i];
    }
    return NULL;
}
static inline esp_err_t stub_nvs_set(const char *k, const void *v, size_t n)
{
    stub_nvs_ent_t *e = stub_nvs_find(k, 1);
    if (!e || n > sizeof(e->val)) return 1;
    memcpy(e->val, v, n);
    e->len = n;
    return ESP_OK;
}
static inline esp_err_t stub_nvs_get(const char *k, void *v, size_t n)
{
    stub_nvs_ent_t *e = stub_nvs_find(k, 0);
    if (!e || e->len != n) return ESP_ERR_NVS_NOT_FOUND;
    memcpy(v, e->val, n);
    return ESP_OK;
}
static inline void stub_nvs_wipe(void) { memset(g_stub_nvs, 0, sizeof(g_stub_nvs)); }

static inline esp_err_t nvs_open(const char *n, int m, nvs_handle_t *h) { (void)n; (void)m; *h = 1; return ESP_OK; }
static inline esp_err_t nvs_get_u32(nvs_handle_t h, const char *k, uint32_t *v) { (void)h; return stub_nvs_get(k, v, 4); }
static inline esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v) { (void)h; return stub_nvs_get(k, v, 1); }
static inline esp_err_t nvs_set_u32(nvs_handle_t h, const char *k, uint32_t v) { (void)h; return stub_nvs_set(k, &v, 4); }
static inline esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v) { (void)h; return stub_nvs_set(k, &v, 1); }
static inline esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t n) { (void)h; return stub_nvs_set(k, v, n); }
static inline esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *v, size_t *n)
{
    (void)h;
    stub_nvs_ent_t *e = stub_nvs_find(k, 0);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    if (e->len > *n) return 1;
    memcpy(v, e->val, e->len);
    *n = e->len;
    return ESP_OK;
}
static inline esp_err_t nvs_erase_key(nvs_handle_t h, const char *k)
{
    (void)h;
    stub_nvs_ent_t *e = stub_nvs_find(k, 0);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    e->used = 0;
    return ESP_OK;
}
static inline esp_err_t nvs_commit(nvs_handle_t h) { (void)h; g_stub_nvs_commits++; return ESP_OK; }
