#include <shuttlesock.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <signal.h>
#define SNOW_ENABLED 1
#include "snow.h"

#undef snow_main_decls
#define snow_main_decls \
        void snow_break() {} \
        void snow_rerun_failed() {raise(SIGSTOP);} \
        struct _snow _snow; \
        int _snow_inited = 0


int set_test_options(int *argc, char **argv) {
  int i = 1;
  while(i < *argc) {
    char *arg = argv[i];
    /*
    if(strcmp(arg, "--multiplier") == 0 && *argc >= i+1) {
      char *val = argv_extract2(argc, argv, i);
      if((repeat_multiplier = atof(val)) == 0.0) {
        printf("invalid --multiplier value %s\n", val);
        return 0;
      }
    }
    else {
      i++;
    }
    */
  }
  return 1;
}

snow_main_decls;
int main(int argc, char **argv) {
  if(!set_test_options(&argc, argv)) {
    return 1;
  }
  return snow_main_function(argc, argv);
}
