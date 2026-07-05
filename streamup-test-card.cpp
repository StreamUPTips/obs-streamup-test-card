#include "test-card-filter.hpp"
#include "version.h"

#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("streamup-test-card", "en-US")

bool obs_module_load(void)
{
	blog(LOG_INFO, "[StreamUP Test Card] Plugin loaded (version %s)", PROJECT_VERSION);
	register_test_card_filter();
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[StreamUP Test Card] Plugin unloaded");
}

const char *obs_module_name(void)
{
	return obs_module_text("Plugin.Name");
}

const char *obs_module_description(void)
{
	return obs_module_text("Plugin.Description");
}
