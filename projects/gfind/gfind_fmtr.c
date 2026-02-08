#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define LINE_SIZE 1024

//NOTE IF YOU MODIFY: you may compile the program with the following: gcc -o gfind_fmtr.c gfind_fmtr.exe
//If using Windows, you may need to install MSYS2 MINGW64 and compile in that terminal, or etc. depending on your situation

// trim leading/trailing whitespace
char *trim(char *str) {
    char *end;

    // trim leading spaces
    while (isspace((unsigned char)*str)) str++;

    if (*str == 0)  // empty string
        return str;

    // trim trailing spaces
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    // write new null terminator
    end[1] = '\0';
    return str;
}

int main() {
    FILE *fp = fopen("input.txt", "r");
    if (!fp) {
        perror("Error opening input.txt");
        return 1;
    }

    FILE *out = fopen("output.json", "w");
    if (!out) {
        perror("Error creating output.json");
        fclose(fp);
        return 1;
    }

    char line[LINE_SIZE];
    int firstEntry = 1;

    fprintf(out, "{\n  \"data\": [\n");

    while (fgets(line, LINE_SIZE, fp)) {
        char *trimmed = trim(line);

        // Skip empty lines or block headers
        if (strlen(trimmed) == 0) continue;
        if (strstr(trimmed, "--- Block") == trimmed) continue;
        if (strstr(trimmed, "Non-redirecting URLs") == trimmed) continue;

        // Expecting lines like: "[Title], URL"
        char *bracketOpen = strchr(trimmed, '[');
        char *bracketClose = strchr(trimmed, ']');

        if (bracketOpen && bracketClose) {
            *bracketClose = '\0';
            char *title = bracketOpen + 1;

            // Look for first comma after the closing bracket
            char *afterBracket = bracketClose + 1;
            char *comma = strchr(afterBracket, ',');

            if (comma) {
                char *url = comma + 1;
                url = trim(url);

                if (!firstEntry) {
                    fprintf(out, ",\n");
                }
                fprintf(out, "    { \"title\": \"%s\", \"url\": \"%s\" }", title, url);
                firstEntry = 0;
            }
        }
    }

    fprintf(out, "\n  ]\n}\n");

    fclose(fp);
    fclose(out);

    printf("✅ JSON successfully written to output.json\n");
    return 0;
}