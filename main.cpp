#include "core/will_engine.h"

int main()
{
    Engine::WillEngine we{};
    we.Initialize();
    we.Run();
    we.Cleanup();

    return 0;
}