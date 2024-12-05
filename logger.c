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

void info(char *message, Logger *logger) {
  fprintf(logger->logfile, "[INFO][%s] %s\n", get_date(), message);
}
void error(char *message, Logger *logger) {
  fprintf(logger->logfile, "[ERROR][%s] %s\n", get_date(), message);
}
void warning(char *message, Logger *logger) {
  fprintf(logger->logfile, "[WARN][%s] %s\n", get_date(), message);
}

void flush(Logger *logger) {
  fflush(logger->logfile);
}

void stop(Logger *logger) {
  fclose(logger->logfile);
  free(logger);
}
