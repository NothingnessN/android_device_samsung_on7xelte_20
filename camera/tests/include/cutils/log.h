// Check printf arguments without emitting the wrapper's verbose logs.
#pragma once

inline void test_log(const char*, ...) __attribute__((format(printf, 1, 2)));
inline void test_log(const char*, ...) {}

#define ALOGV(...) test_log(__VA_ARGS__)
#define ALOGI(...) test_log(__VA_ARGS__)
#define ALOGE(...) test_log(__VA_ARGS__)
