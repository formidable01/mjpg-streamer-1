#ifndef OUTPUT_WS_H_
#define OUTPUT_WS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "../../mjpg_streamer.h"
#include "../../utils.h"

int output_init(output_parameter *param);
int output_stop(int id);
int output_run(int id);
int output_cmd(int plugin, unsigned int control_id, unsigned int group, int value);

#ifdef __cplusplus
}
#endif

#endif /* OUTPUT_WS_H_ */