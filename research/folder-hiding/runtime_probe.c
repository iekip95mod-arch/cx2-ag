#include <os.h>
#include <errno.h>

#ifndef PROBE_MODE
#define PROBE_MODE 0
#endif

static int path_state(const char *path, struct stat *st)
{
    if (stat(path, st) == 0)
        return 1;
    return errno == ENOENT ? 0 : -1;
}

static int regular_file(const char *path)
{
    struct stat st;
    return path_state(path, &st) == 1 && S_ISREG(st.st_mode) && st.st_size > 0;
}

static int move_new(const char *source, const char *destination)
{
    struct stat st;
    if (path_state(destination, &st) != 0)
        return -1;
    return rename(source, destination);
}

#if PROBE_MODE == 0
static int run_probe(FILE *log)
{
    const char *staged = "/documents/calc_helpers.luax.tns";
    const char *installed = "/appdata/ndl/calc_helpers.luax.tns";
    if (!regular_file(staged) || !regular_file(installed))
        return -1;
    char backup[128];
    unsigned number;
    for (number = 1; number <= 9999; ++number) {
        snprintf(backup, sizeof backup, "/appdata/ndl/calc_helpers.backup%04u.luax.tns", number);
        struct stat st;
        int state = path_state(backup, &st);
        if (state < 0)
            return -1;
        if (!state)
            break;
    }
    if (number > 9999 || move_new(installed, backup))
        return -1;
    fprintf(log, "bridge backup %s\n", backup);
    fflush(log);
    if (move_new(staged, installed)) {
        int rollback = move_new(backup, installed);
        fprintf(log, "bridge install failed, rollback rc=%d\n", rollback);
        return -1;
    }
    fprintf(log, "bridge installed %s\n", installed);
    return 0;
}
#elif PROBE_MODE == 1
static int run_probe(FILE *log)
{
    const char *documents = "/documents/ndl";
    const char *external = "/appdata/ndl";
    struct stat doc_stat, external_stat;
    int doc_state = path_state(documents, &doc_stat);
    int external_state = path_state(external, &external_stat);
    if (doc_state < 0 || external_state < 0 || doc_state == external_state)
        return -1;
    const char *source = doc_state ? documents : external;
    const char *destination = doc_state ? external : documents;
    if (!S_ISDIR(doc_state ? doc_stat.st_mode : external_stat.st_mode))
        return -1;
    char resource[128], persistent[128];
    snprintf(resource, sizeof resource, "%s/ndl_resources.tns", source);
    snprintf(persistent, sizeof persistent, "%s/persistent.tns", source);
    if (!regular_file(resource) || !regular_file(persistent))
        return -1;
    struct stat parent_stat;
    int parent_state = path_state(doc_state ? "/appdata" : "/documents", &parent_stat);
    if (parent_state < 0 || (parent_state && !S_ISDIR(parent_stat.st_mode)))
        return -1;
    if (!parent_state && mkdir(doc_state ? "/appdata" : "/documents", 0700))
        return -1;
    if (move_new(source, destination))
        return -1;
    fprintf(log, "layout %s to %s\n", source, destination);
    return 0;
}
#else
#error unsupported runtime probe mode
#endif

int main(void)
{
    if (nl_isstartup())
        return 1;
    FILE *log = fopen("/documents/RuntimeProbeStatus.tns", "a");
    if (!log)
        return 1;
    int rc = run_probe(log);
    fprintf(log, "runtime probe mode=%d rc=%d\n", PROBE_MODE, rc);
    if (fclose(log))
        rc = -1;
    refresh_osscr();
    return rc != 0;
}
