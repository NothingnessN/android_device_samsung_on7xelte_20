// Host tests provide a controlled implementation of sync_wait.
#pragma once
extern "C" int sync_wait(int fd, int timeout);
