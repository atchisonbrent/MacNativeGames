// Strong overrides of production weak defaults, kept in a separate TU.
int test_fps = 60;
int dm_fps_setting(void) { return test_fps; }
int front_display_hz(void) { return 120; } // Never query a real display.
