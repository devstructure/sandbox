#ifndef MESSAGE_H
#define MESSAGE_H

#define MESSAGE_QUIET_DEFAULT (-1)

int message_init(const char *progname);
void message_free();
void message_quiet_default(int quiet);
void message_quiet(int quiet);
int message(const char *format, ...);
int message_loud(const char *format, ...);

#endif
