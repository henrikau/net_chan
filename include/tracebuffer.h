/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>

FILE * tb_open(void);
void tb_close(FILE *tracefd);
void tb_tag(FILE *tracefd, const char *line);
#ifdef __cplusplus
}
#endif
