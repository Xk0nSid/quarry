#include <stdio.h>

#define QUARRY_IMPLEMENTATION
#include "quarry.h"

int main(void) {
    char *s = {0};
    qry_sb_printf(&s, "%d is ", 69);
    qry_sb_append(&s, QRY_S("sixty"));
    qry_sb_append(&s, QRY_S(" nine"));

    qry_Str line = qry_sb_view(s);
    printf(QRY_STR_FMT "\n", QRY_STR_ARG(line));

    while (line.len) {
        qry_Str token = qry_str_chop(&line, ' ');
        printf("Token: " QRY_STR_FMT "\n", QRY_STR_ARG(token));
    }

    return 0;
}
