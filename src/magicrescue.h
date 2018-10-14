#ifndef _MAGICRESCUE_H

#include "util.h"

enum OUTPUT_MODE { OUT_HUMAN = 0, OUT_I = 1, OUT_O = 2, OUT_IO = 3 };
enum NAME_MODE { MODE_DEVICE, MODE_FILES };

struct progress {
    off_t position;
    char device[PATH_MAX];
    char device_basename[PATH_MAX];
};

extern char *output_dir;
extern enum OUTPUT_MODE machine_output;
extern enum NAME_MODE name_mode;
extern struct progress progress;

/*
 * Extraction
 */
void compose_name(char *name, off_t offset, const char *extension);
int run_shell(int fd, off_t offset, const char *command,
	const char *argument, int *stdout_pipe);
void rename_output(int fd, off_t offset, const char *command,
	char *origname);
off_t extract(int fd, struct recipe *r, off_t offset);

# define _MAGICRESCUE_H
#endif /* _MAGICRESCUE_H */
