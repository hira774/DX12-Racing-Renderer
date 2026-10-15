#include "Application.h"

int main()
{
	auto& app = Application::Instance();

	if (!app.Init()) { return -1; }

	app.Run();

	app.Terminate();

	return 0;
}
