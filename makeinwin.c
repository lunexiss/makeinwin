#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_LINE 512
#define MAKEFILE_NAME "Makefile"
#define MAX_TARGETS 64
#define MAX_DEPS 16

typedef struct {
    char *name;
    char *value;
} Variable;

typedef struct {
    char name[64];
    char *commands[16];
    int command_count;
    char *deps[MAX_DEPS];
    int dep_count;
    int visited;
} Target;

Target targets[MAX_TARGETS];
int target_count = 0;

Variable *variables = NULL;
int variable_count = 0;

char *strdup_trim(const char *src) {
    if (!src) return NULL;
    while (*src == ' ' || *src == '\t') src++;
    char *dup = _strdup(src);
    if (!dup) return NULL;
    char *end = dup + strlen(dup) - 1;
    while (end > dup && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) *end-- = '\0';
    return dup;
}

// damn, this shit is magic
char *expand_variables(const char *input) {
    static char output[4096]; // big enough for anyone
    char *out = output;
    const char *p = input;

    if (!input) {
        output[0] = '\0';
        return output;
    }

    while (*p) {
        if (*p == '$' && *(p + 1) == '(') {
            p += 2;
            char varname[128] = {0};
            int vi = 0;
            while (*p && *p != ')' && vi < 127)
                varname[vi++] = *p++;
            if (*p == ')') p++;

            const char *val = "";
            for (int i = 0; i < variable_count; i++) {
                if (strcmp(variables[i].name, varname) == 0) {
                    val = variables[i].value;
                    break;
                }
            }
            out += sprintf(out, "%s", val);
        } else {
            *out++ = *p++;
        }
    }
    *out = '\0';
    return output;
}

void set_variable(const char *name, const char *value) {
    for (int i = 0; i < variable_count; i++) {
        if (strcmp(variables[i].name, name) == 0) {
            free(variables[i].value);
            variables[i].value = _strdup(value);
            return;
        }
    }
    variables = realloc(variables, sizeof(Variable) * (variable_count + 1));
    variables[variable_count].name = _strdup(name);
    variables[variable_count].value = _strdup(value);
    variable_count++;
}

Target *find_target(const char *name) {
    for (int i = 0; i < target_count; i++) {
        if (strcmp(targets[i].name, name) == 0)
            return &targets[i];
    }
    return NULL;
}

int parse_makefile() {
    FILE *file = fopen(MAKEFILE_NAME, "r");
    if (!file) return 0;

    char line[MAX_LINE];
    Target *current = NULL;

    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0') continue;

        // fuck off comments
        if (line[0] == '#') continue;

        if (strchr(line, '=') && !strchr(line, ':')) {
            char *eq = strchr(line, '=');
            *eq = '\0';
            char *name = strdup_trim(line);
            char *value = strdup_trim(eq + 1);
            if (name && value) {
                set_variable(name, expand_variables(value));
            }
            free(name);
            free(value);
            continue;
        }

        if (line[0] != ' ' && line[0] != '\t') {
            char *colon = strchr(line, ':');
            if (!colon) continue;

            *colon = '\0';
            Target *t = &targets[target_count++];
            char *target_name = strdup_trim(line);
            if (target_name) {
                strcpy(t->name, expand_variables(target_name));
                free(target_name);
            }

            t->command_count = 0;
            t->dep_count = 0;
            t->visited = 0;

            char *dep_line = strdup_trim(colon + 1);
            if (dep_line && strlen(dep_line) > 0) {
                char *expanded_deps = _strdup(expand_variables(dep_line));
                char *token = strtok(expanded_deps, " \t");
                while (token && t->dep_count < MAX_DEPS) {
                    t->deps[t->dep_count++] = _strdup(expand_variables(token));
                    token = strtok(NULL, " \t");
                }
                free(expanded_deps);
            }
            free(dep_line);

            current = t;
        } else if (current) {
            char *cmd = strdup_trim(line);
            if (cmd && strlen(cmd) > 0) {
                current->commands[current->command_count++] = _strdup(expand_variables(cmd));
            }
            free(cmd);
        }
    }

    fclose(file);
    return 1;
}

int execute_command(const char *command) {
    printf("running: %s\n", command);
    return system(command);
}

int run_target(const char *name) {
    Target *t = find_target(name);
    if (!t) {
        printf("target '%s' not found in Makefile.\n", name);
        return 1;
    }

    if (t->visited) return 0;  // FUCK duplicate
    t->visited = 1;

    for (int i = 0; i < t->dep_count; i++) {
        if (!t->deps[i] || strlen(t->deps[i]) == 0) {
            printf("warning: empty dependency found for target '%s'\n", name);
            continue;
        }

        // claude ty for fixing :3 this stupid shit
        Target *dep = find_target(t->deps[i]);
        if (dep) {
            int result = run_target(dep->name);
            if (result != 0) return result;
        } else {
            // check if the file was not fucked
            DWORD attrs = GetFileAttributesA(t->deps[i]);
            if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                printf("missing dependency file: '%s' for target '%s'\n", t->deps[i], name);
                return 1;
            }
        }
    }

    // execute commands
    for (int i = 0; i < t->command_count; i++) {
        if (t->commands[i] && strlen(t->commands[i]) > 0) {
            int result = execute_command(t->commands[i]);
            if (result != 0) {
                printf("command failed with exit code %d\n", result);
                return result;
            }
        }
    }

    return 0;
}

// fallback garbage in case user has fucked Makefile
void build() {
    printf("compiling...\n");
    int result = system("gcc main.c -o main.exe");
    if (result != 0) {
        printf("build failed.\n");
    } else {
        printf("build succeeded.\n");
    }
}

void clean() {
    printf("cleaning...\n");

    DWORD attrs = GetFileAttributesA("main.exe");
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        printf("couldn't delete file or already clean.\n");
        return;
    }

    int result = system("del /f /q main.exe");
    if (result != 0) {
        printf("couldn't delete file or already clean.\n");
    } else {
        printf("clean done.\n");
    }
}

int main(int argc, char *argv[]) {
    const char *target_name = NULL;
    
    // parse shi
    int makefile_exists = parse_makefile();
    
    if (argc >= 2) {
        target_name = argv[1];
    } else {
        if (makefile_exists && target_count > 0) {
            // defaulting shit :3
            target_name = targets[0].name;
            printf("no target found, defaulting to '%s'\n", target_name);
        } else if (makefile_exists) {
            fprintf(stderr, "no targets found in Makefile.\n");
            return 1;
        } else {
            target_name = "all";
            printf("no Makefile found, defaulting to 'all'\n");
        }
    }

    if (!makefile_exists) {
        printf("no Makefile found falling back to builtin.\n");

        if (strcmp(target_name, "all") == 0) {
            build();
        } else if (strcmp(target_name, "clean") == 0) {
            clean();
        } else {
            printf("unknown target: %s\n", target_name);
            return 1;
        }

        return 0;
    }

    return run_target(target_name);
}