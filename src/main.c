#include <daemon.h>
#include <micarrayd.h>

int main(int argc, char *argv[])
{
	return daemonize(micarrayd, "micarrayd", argc, argv);
}

