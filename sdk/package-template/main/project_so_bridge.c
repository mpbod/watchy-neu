extern const void *watchy_package_entry(void);

/* A direct undefined reference makes project_so pull the C++ package object
 * from libmain.a. Hidden visibility prevents this bridge from becoming an ABI
 * export. */
__attribute__((used))
static const void *(*const watchy_project_so_entry)(void) = watchy_package_entry;
