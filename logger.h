#ifndef LOGGER_H
#define LOGGER_H
#import <stdio.h>
#import <stdlib.h>
#include <time.h>

typedef struct {
  FILE *logfile;
} Logger;

Logger* createLogger();
void info(char *message, Logger *logger);
void error(char *message, Logger *logger);
void warning(char *message, Logger *logger);
void flush(Logger *logger);
void stop(Logger *logger);
#endif
