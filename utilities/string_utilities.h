#pragma once

/* from
 * http://coding.debuntu.org/c-implementing-str_replace-replace-all-occurrences-substring#comment-722
 */
char *str_replace(const char *string, const char *substr, const char *replacement);

char *service_name(const char *raw_service_name);

// Read an entire file into a newly-allocated, null-terminated buffer.
// Returns NULL (with errno set) if the file cannot be opened or read.
// The caller must free the returned buffer.
char *read_file_to_string(const char *pathname);

// Expand ${NAME} environment-variable references in the given text, taking the
// values from the process environment. "${NAME}" (NAME matching
// [A-Za-z_][A-Za-z0-9_]*) is replaced by the value of NAME; "$${" yields a
// literal "${"; a reference to an unset variable is a fatal error. Text with no
// "${" is returned unchanged. name_for_errors identifies the text in error
// messages. Returns a newly-allocated, null-terminated string to be freed by
// the caller.
char *expand_environment_variables(const char *text, const char *name_for_errors);