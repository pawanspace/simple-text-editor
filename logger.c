#import "logger.h"
#include <string.h>

Logger* createLogger() {
  Logger *logger = (Logger*)malloc(sizeof(Logger));
  time_t current_time = time(NULL);
  char filename[100];
  snprintf(filename, sizeof(filename), "Kilo-%ld.log", current_time);
  logger->logfile = fopen(filename, "wx");
  return logger;
}

char* get_date() {
    time_t now;
    time(&now);
    char *date = ctime(&now);
    date[strlen(date) - 1] = '\0'; // Remove newline
    return date;
}


void info(Logger *logger, const char *fmt, ...) {
    if (!logger || !logger->logfile) return;    
    fprintf(logger->logfile, "[%s] [INFO] ", get_date());
   
    va_list args;
    va_start(args, fmt);
    vfprintf(logger->logfile, fmt, args);
    va_end(args);
    
    fprintf(logger->logfile, "\n");
}

void error(Logger *logger, const char *fmt, ...) {
    if (!logger || !logger->logfile) return;
    fprintf(logger->logfile, "[%s] [ERROR] ", get_date());
    
    va_list args;
    va_start(args, fmt);
    vfprintf(logger->logfile, fmt, args);
    va_end(args);
    
    fprintf(logger->logfile, "\n");
}

void warning(Logger *logger, const char *fmt, ...) {
    if (!logger || !logger->logfile) return;
    fprintf(logger->logfile, "[%s] [WARNING] ", get_date());
    va_list args;
    va_start(args, fmt);
    vfprintf(logger->logfile, fmt, args);
    va_end(args);
    
    fprintf(logger->logfile, "\n");
}

void flush(Logger *logger) {
  if (!logger || !logger->logfile) return;   
  fflush(logger->logfile);
}

void stop(Logger *logger) {
  if (!logger || !logger->logfile) return;   
  fclose(logger->logfile);
  free(logger);
}
