#include <assert.h>
#include "../runtime/utf16_access.h"
#define __ANDROID__
#include "../runtime/offline.c"

static media_item_state sample_items[] = {{.track_id = 21}, {.track_id = 22}};
static int last_command, calls, notifications;
static const char *android_state = "0:";
static char requested[16];

uint32_t rt_arg(cpu_t *c, int k) { return c->r[k]; }
id rt_retain(id value) { return value; }
id rt_autorelease(id value) { return value; }
void rt_release(cpu_t *c, id value) { (void)c; (void)value; }
int rt_is_kind(id obj, id cls) { return cls == cls_media_item && (obj == 11 || obj == 12); }
void *rt_objc_state(id obj, size_t size) {
    assert(size == sizeof(media_item_state) && obj >= 11 && obj <= 12);
    return &sample_items[obj - 11];
}
const char *rt_nsstring_utf8(id value) { return value == 21 ? "a" : value == 22 ? "b" : ""; }
id rt_nsstring_new(const char *text) { (void)text; return 30; }
id rt_nsdict_get(id dictionary, id key) { (void)dictionary; (void)key; return 0; }
uint32_t rt_nsarray_count(id array) { return array == 7 ? 2 : 0; }
id rt_nsarray_at(id array, uint32_t index) { assert(array == 7); return 11 + index; }
double rt_now(void) { static double time; return ++time; }
void rt_notify(cpu_t *c, const char *name, id obj, id info) {
    (void)c; (void)name; (void)info; assert(obj == ipod_player); notifications++;
}
void rt_platform_music_command(int command, const char *const *ids, size_t count) {
    last_command = command;
    calls++;
    if (count) snprintf(requested, sizeof requested, "%s", ids[0]);
}
void rt_platform_music_state(char state[40]) { strcpy(state, android_state); }

int main(void) {
    cpu_t c = {0};
    mu_play(&c, NULL); assert(calls == 0);
    ipod_player = 1; c.r[0] = 1;
    mu_observe(&c, NULL);
    music_queue = 7;
    mu_play(&c, NULL); assert(last_command == RT_MUSIC_PLAY && !strcmp(requested, "a"));
    android_state = "1:a"; rt_offline_music_tick(&c); assert(notifications == 2);
    mu_current(&c, NULL); assert(c.r[0] == 11);
    mu_pause(&c, NULL); assert(last_command == RT_MUSIC_PAUSE);
    android_state = "2:a"; rt_offline_music_tick(&c); assert(notifications == 3);
    c.r[2] = 12; mu_set_item(&c, NULL);
    assert(last_command == RT_MUSIC_SELECT && !strcmp(requested, "b"));
    mu_current(&c, NULL); assert(c.r[0] == 12);
    mu_play(&c, NULL); assert(!strcmp(requested, "b"));
    android_state = "1:b"; rt_offline_music_tick(&c); assert(notifications == 5);
    mu_current(&c, NULL); assert(c.r[0] == 12);
    c.r[2] = 11; mu_set_item(&c, NULL); /* selection during playback starts A */
    assert(last_command == RT_MUSIC_PLAY);
    android_state = "1:a"; rt_offline_music_tick(&c);
    android_state = "1:b"; rt_offline_music_tick(&c); /* backend advanced, UI follows B */
    mu_current(&c, NULL); assert(c.r[0] == 12 && !music_selection_pending);
    mu_next(&c, NULL); assert(last_command == RT_MUSIC_NEXT);
    mu_prev(&c, NULL); assert(last_command == RT_MUSIC_PREVIOUS);
    mu_stop(&c, NULL); assert(last_command == RT_MUSIC_STOP);
    const char *title = "Andr\xc3\xa9 - \xf0\x9f\x8e\xb5";
    assert(utf16_units(title, title + strlen(title)) == 10);
    assert(utf16_unit_at(title, 4) == 0xe9);
    assert(utf16_unit_at(title, 8) == 0xd83c && utf16_unit_at(title, 9) == 0xdfb5);
    assert(!strcmp(utf16_boundary(title, 5), " - \xf0\x9f\x8e\xb5"));
    puts("Original player guest selectors route selection, playback, pause, skips, stop and notifications.");
}
