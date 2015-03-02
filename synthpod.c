#include <Eina.h>

#include <app.h>

int
main(int argc, char **argv)
{
	eina_init();

	app_t *app = app_new();
	app_run(app);
	app_free(app);
	
	eina_shutdown();

	return 0;
}
