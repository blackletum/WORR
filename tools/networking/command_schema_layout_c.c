/* Compile the public command schema and stream declarations as strict C11. */
#include "common/net/command_stream.h"

int main(void)
{
    worr_command_id_v1 command_id = {0};
    worr_command_cursor_v1 cursor = {0};
    worr_command_record_v1 record = {0};
    worr_command_stream_slot_v1 slot = {0};

    return sizeof(command_id) == 8 && sizeof(cursor) == 8 &&
                   sizeof(record) == 104 && sizeof(slot) == 128
               ? 0
               : 1;
}
