#ifndef TERMUTIL_H
#define TERMUTIL_H

void enable_raw_mode();
void disable_raw_mode();
void enable_canonical_mode();
void telnet_setup();
int filter_telnet_iac(unsigned char *buf, int len);
#endif // TERMUTIL_H
