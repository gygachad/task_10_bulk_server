#include "async.h"
#include "cmd_processor.h"

namespace async 
{

handle_t connect(std::size_t bulk) 
{
    cmd_processor* cmd_proc = new cmd_processor(bulk);

    return static_cast<handle_t>(cmd_proc);
}

void receive(handle_t handle, const char *data, std::size_t size) 
{
    if (!handle)
        return;

    stringstream sstream(data);

    cmd_processor* cmd_proc = static_cast<cmd_processor*>(handle);

    string cmd;
    while (getline(sstream, cmd))
    {
        if(cmd != "")
            cmd_proc->add_cmd(cmd);
    }
}

void disconnect(handle_t handle) 
{
    cmd_processor* cmd_proc = static_cast<cmd_processor*>(handle);

    if (cmd_proc)
        delete cmd_proc;
}

}
