#pragma once

// impulse response filter file status
typedef enum { ev_unchecked, ev_okay, ev_invalid } ir_file_evaluation;

// impulse response filter file record
typedef struct {
  unsigned int samplerate; // initialized to 0, will be filter frame rate
  unsigned int channels;
  char *filename; // the parsed filename
} ir_file_info_t;

/* Parse comma-separated filenames with optional quotes from the input string
 * Returns array of ir_file_info_t structs (caller must free both array and filenames)
 * count is set to number of filenames found
 * Returns NULL on error
 */
ir_file_info_t *parse_ir_filenames(const char *input, unsigned int *file_count);
// Access: files[i].filename, files[i].rate, files[i].evaluation

/* Do a quick sanity check on the files -- see if they can be opened as sound files */
unsigned int sanity_check_ir_files(const int option_print_level, ir_file_info_t *files,
                                   unsigned int count);

/* Free the array returned by parse_filenames */
void free_ir_filenames(ir_file_info_t *files, unsigned int file_count);
