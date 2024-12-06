#ifndef LOGGER_H
#define LOGGER_H
#import <stdio.h>
#import <stdlib.h>
#include <time.h>
#include <stdarg.h>  // Added for variable arguments

typedef struct {
  FILE *logfile;
} Logger;

Logger* createLogger();
void info(Logger *logger, const char *fmt, ...);
void error(Logger *logger, const char *fmt, ...);
void warning(Logger *logger, const char *fmt, ...);
void flush(Logger *logger);
void stop(Logger *logger);
#endif
