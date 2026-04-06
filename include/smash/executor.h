#ifndef SMASH_EXECUTOR_H
#define SMASH_EXECUTOR_H

#include "parser.h"
#include "state.h"

int smash_execute_pipeline(SmashState *state, const SmashPipeline *pipeline);

#endif
