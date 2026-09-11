#include <os.h>
#include <lauxlib.h>
#include "storage.h"

static int restore(lua_State *L)
{
	char message[256];
	int rc = storage_restore(get_documents_dir(), "/appdata/test_app/store", message, sizeof message);
	lua_pushboolean(L, rc == 0);
	lua_pushstring(L, rc == 0 ? "" : message);
	return 2;
}

static const luaL_reg methods[] = {
	{ "restore", restore },
	{ NULL, NULL }
};

int main(void)
{
	lua_State *L = nl_lua_getstate();
	if (!L)
		return 1;
	luaL_register(L, "calc_helpers", methods);
	_exit(0);
}
