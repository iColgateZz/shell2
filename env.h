typedef struct Env
{
    char *name;
    char *value;
    struct Env *next;
} Env;

char *psh_getenv(char *name);
void psh_setenv(char *name, char *value);
void psh_unsetenv(char *name);
void read_config_file();
char *configure_prompt(char *env);
void expand(char **tokens);
void free_env_list();