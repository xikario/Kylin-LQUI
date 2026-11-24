#include "AgentClient.h"
#include "CodexClient.h"
#include "AcpClient.h"
#include "CommandCodeClient.h"

AgentClient *createAgentClient(const Settings &settings,QObject *parent) {
    AgentClient *client=settings.backend=="codex"?static_cast<AgentClient*>(new CodexClient(parent))
        :settings.backend=="commandcode"?static_cast<AgentClient*>(new CommandCodeClient(parent)):new AcpClient(parent);
    client->configure(settings);
    return client;
}
