/* vim: set sw=2: */
#define LUA_LIB
#include "lbind.h"


#include <assert.h>
#include <string.h>


static int relate_index(int idx, int onstack) {
  return (idx > 0 || idx <= LUA_REGISTRYINDEX)
    ? idx
    : idx - onstack;
}

/* compatible apis */
#if LUA_VERSION_NUM < 502
#  define LUA_OK                        0
#  define lua_getuservalue              lua_getfenv
#  define lua_setuservalue              lua_setfenv
#  define lua_rawlen                    lua_objlen

static lua_Integer lua_tointegerx(lua_State *L, int idx, int *valid) {
  lua_Integer n;
  *valid = (n = lua_tointeger(L, idx)) != 0 || lua_isnumber(L, idx);
  return n;
}

static void lua_rawgetp(lua_State *L, int idx, const void *p) {
  lua_pushlightuserdata(L, (void*)p);
  lua_rawget(L, relate_index(idx, 1));
}

static void lua_rawsetp(lua_State *L, int idx, const void *p) {
  lua_pushlightuserdata(L, (void*)p);
  lua_insert(L, -2);
  lua_rawset(L, relate_index(idx, 1));
}

void luaL_setfuncs(lua_State *L, luaL_Reg *l, int nup) {
  luaL_checkstack(L, nup, "too many upvalues");
  for (; l->name != NULL; l++) {  /* fill the table with given functions */
    int i;
    for (i = 0; i < nup; i++)  /* copy upvalues to the top */
      lua_pushvalue(L, -nup);
    lua_pushcclosure(L, l->func, nup);  /* closure with those upvalues */
    lua_setfield(L, -(nup + 2), l->name);
  }
  lua_pop(L, nup);  /* remove upvalues */
}

const char *luaL_tolstring(lua_State *L, int idx, size_t *plen) {
  if (!luaL_callmeta(L, idx, "__tostring")) {  /* no metafield? */
    switch (lua_type(L, idx)) {
      case LUA_TNUMBER:
      case LUA_TSTRING:
        lua_pushvalue(L, idx);
        break;
      case LUA_TBOOLEAN:
        lua_pushstring(L, (lua_toboolean(L, idx) ? "true" : "false"));
        break;
      case LUA_TNIL:
        lua_pushliteral(L, "nil");
        break;
      default:
        lua_pushfstring(L, "%s: %p", luaL_typename(L, idx),
            lua_topointer(L, idx));
        break;
    }
  }
  return lua_tolstring(L, -1, plen);
}

void luaL_traceback(lua_State *L, lua_State *L1, const char *msg, int level) {
  lua_Debug ar;
  int top = lua_gettop(L);
  int numlevels = countlevels(L1);
  int mark = (numlevels > LEVELS1 + LEVELS2) ? LEVELS1 : 0;
  if (msg) lua_pushfstring(L, "%s\n", msg);
  lua_pushliteral(L, "stack traceback:");
  while (lua_getstack(L1, level++, &ar)) {
    if (level == mark) {  /* too many levels? */
      lua_pushliteral(L, "\n\t...");  /* add a '...' */
      level = numlevels - LEVELS2;  /* and skip to last ones */
    }
    else {
      lua_getinfo(L1, "Slnt", &ar);
      lua_pushfstring(L, "\n\t%s:", ar.short_src);
      if (ar.currentline > 0)
        lua_pushfstring(L, "%d:", ar.currentline);
      lua_pushliteral(L, " in ");
      pushfuncname(L, &ar);
      if (ar.istailcall)
        lua_pushliteral(L, "\n\t(...tail calls...)");
      lua_concat(L, lua_gettop(L) - top);
    }
  }
  lua_concat(L, lua_gettop(L) - top);
}

#endif /* LUA_VERSION_NUM < 502 */


/* lbind information hash routine */

#define LBIND_POINTERBOX 0x1B9DBB81
#define LBIND_TYPEBOX    0x1BD29B81

static void get_pointerbox(lua_State *L) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)LBIND_POINTERBOX);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushliteral(L, "v");
    lbind_setmetafield(L, -2, "__index");
	lua_pushvalue(L, -1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, (void*)LBIND_POINTERBOX);
  }
}

static void get_typebox(lua_State *L) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)LBIND_TYPEBOX);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
	lua_pushvalue(L, -1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, (void*)LBIND_TYPEBOX);
  }
}


/* lbind class register */

void lbind_install(lua_State *L, lbind_Reg *libs) {
  lua_getfield(L, LUA_REGISTRYINDEX, "_PRELOAD"); /* 1 */
  if (libs != NULL) {
    for (; libs->name != NULL; ++libs) {
      lua_pushstring(L, libs->name); /* 2 */
      lua_pushcfunction(L, libs->open_func); /* 3 */
      lua_rawset(L, -3); /* 2,3->1 */
    }
  }
  lua_pushstring(L, "lbind"); /* 2 */
  lua_pushcfunction(L, luaopen_lbind); /* 3 */
  lua_rawset(L, -3); /* 2,3->1 */
  lua_pop(L, 1); /* (1) */
}

int lbind_requiref(lua_State *L, const char *name, lua_CFunction loader) {
  lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED"); /* 1 */
  lua_getfield(L, -1, name); /* 2 */
  if (!lua_isnil(L, -1)) {
    lua_remove(L, -2); /* (1) */
    return 0;
  }
  lua_pop(L, 1);
  lua_pushstring(L, name); /* 2 */
  lua_pushcfunction(L, loader); /* 3 */
  lua_pushvalue(L, -2); /* 2->4 */
  lua_call(L, 1, 1); /* 3,4->3 */
  lua_pushvalue(L, -1); /* 3->4 */
  lua_insert(L, -4); /* 4->1 */
  /* stack: lib _LOADED name lib */
  lua_rawset(L, -3); /* 3,4->2 */
  lua_pop(L, 1); /* (2) */
  return 1;
}

void lbind_requirelibs(lua_State *L, lbind_Reg *libs) {
  lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED"); /* 1 */
  for (; libs->name != NULL; ++libs) {
    lua_pushstring(L, libs->name); /* 2 */
    lua_pushvalue(L, -1); /* 3 */
    lua_rawget(L, -3); /* 3->3 */
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 2);
      continue;
    }
    lua_pop(L, 1);
    lua_pushcfunction(L, libs->open_func); /* 3 */
    lua_pushvalue(L, -2); /* 2->4 */
    lua_call(L, 1, 1); /* 3,4->3 */
    lua_rawset(L, -5); /* 2,3->1 */
  }
  lua_pop(L, 1);
}

void lbind_requireinto(lua_State *L, const char *prefix, lbind_Reg *libs) {
  /* stack: table */
  lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED"); /* 1 */
  for (; libs->name != NULL; ++libs) {
    lua_pushstring(L, libs->name); /* 2 */
    if (prefix == NULL)
      lua_pushvalue(L, -1); /* 3 */
    else
      lua_pushfstring(L, "%s.%s", prefix, libs->name); /* 3 */
    lua_pushvalue(L, -1); /* 4 */
    lua_rawget(L, -4); /* 4->4 */
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1); /* (4) */
      lua_pushcfunction(L, libs->open_func); /* 4 */
      lua_pushvalue(L, -2); /* 3->5 */
      lua_call(L, 1, 1); /* 4,5->4 */
      lua_pushvalue(L, -2); /* 3->5 */
      lua_pushvalue(L, -2); /* 4->6 */
      /* stack: table [_LOADED name prefix.name ret prefix.name ret] */
      lua_rawset(L, -6); /* 4,5->1 */
    }
    lua_remove(L, -2); /* (3) */
    lua_rawset(L, -4); /* 2,3->table */
  }
  lua_pop(L, 1);
}


/* lbind utils functions */

int lbind_argferror(lua_State *L, int idx, const char *fmt, ...) {
    const char *errmsg;
    va_list argp;
    va_start(argp, fmt);
    errmsg = lua_pushvfstring(L, fmt, argp);
    va_end(argp);
    return luaL_argerror(L, idx, errmsg);
}

int lbind_typeerror(lua_State *L, int idx, const char *tname) {
  const char *real_type = lbind_type(L, idx);
  return lbind_argferror(L, idx, "%s expected, got %s", tname,
      real_type != NULL ? real_type : luaL_typename(L, idx));
}

int lbind_matcherror(lua_State *L, const char *extramsg) {
  lua_Debug ar;
  lua_getinfo(L, "n", &ar);
  if (ar.name == NULL)
    ar.name = "?";
  return luaL_error(L, "no matching functions for call to %s\n"
      "candidates are:\n%s", ar.name, extramsg);
}

int lbind_copystack(lua_State *from, lua_State *to, int n) {
    int i;
    luaL_checkstack(from, n, "too many args");
    for (i = 0; i < n; ++i)
        lua_pushvalue(from, -n);
    lua_xmove(from, to, n);
    return n;
}

const char *lbind_dumpstack(lua_State *L, const char *msg) {
  int i, top = lua_gettop(L);
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  luaL_addstring(&b, "dump stack: ");
  luaL_addstring(&b, msg != NULL ? msg : "");
  luaL_addstring(&b, "\n---------------------------\n");
  for (i = 1; i <= top; ++i) {
    lua_pushfstring(L, "%d: ", i);
    luaL_addvalue(&b);
    lbind_tolstring(L, i, NULL);
    luaL_addvalue(&b);
    luaL_addstring(&b, "\n");
  }
  luaL_addstring(&b, "---------------------------\n");
  luaL_pushresult(&b);
  return lua_tostring(L, -1);
}

int lbind_hasfield(lua_State *L, int idx, const char *field) {
  int hasfield;
  lua_getfield(L, idx, field);
  hasfield = !lua_isnil(L, -1);
  lua_pop(L, 1);
  return hasfield;
}

static int Ltraceback(lua_State *L) {
    const char *msg = lua_tostring(L, 1);
    if (msg)
        luaL_traceback(L, L, msg, 1);
    else if (!lua_isnoneornil(L, 1)) {  /* is there an error object? */
        if (!luaL_callmeta(L, 1, "__tostring"))  /* try its 'tostring' metamethod */
            lua_pushliteral(L, "(no error message)");
    }
    return 1;
}

int lbind_self(lua_State *L, const void *p, const char *method, int nargs, int *ptraceback) {
  luaL_checkstack(L, nargs+3, "too many arguments to self call");
  if (!lbind_retrieve(L, p)) return 0; /* 1 */
  lua_getfield(L, -1, method); /* 2 */
  if (lua_isnil(L, -1)) {
    lua_pop(L, 2);
    return 0;
  }
  if (ptraceback) {
    lua_pushcfunction(L, Ltraceback);
    lua_insert(L, -3);
    *ptraceback = lua_gettop(L) - 3;
  }
  lua_insert(L, -2);
  /* stack: traceback method object */
  return 1;
}


/* metatable utils */

int lbind_setmetatable(lua_State *L, const void *t) {
  if (lbind_getmetatable(L, t)) {
    lua_setmetatable(L, -2);
    return 1;
  }
  return 0;
}

int lbind_getmetatable(lua_State *L, const void *t) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, t);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  return 1;
}

int lbind_setmetafield(lua_State *L, int idx, const char *field) {
  int newmt = 0;
  if (!lua_getmetatable(L, idx)) {
    lua_createtable(L, 0, 1);
    lua_pushvalue(L, -1);
    lua_setmetatable(L, relate_index(idx, 2));
    newmt = 1;
  }
  lua_pushvalue(L, -2);
  lua_setfield(L, -2, field);
  lua_pop(L, 2);
  return newmt;
}

static int Llibcall(lua_State *L) {
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_rawget(L, 1);
  if (lua_isnil(L, -1)) {
    lua_pushfstring(L, "no such method (%s)", lua_tostring(L, lua_upvalueindex(1)));
    return luaL_argerror(L, 1, lua_tostring(L, -1));
  }
  lua_replace(L, 1);
  lua_call(L, lua_gettop(L)-1, LUA_MULTRET);
  return lua_gettop(L);
}

int lbind_setlibcall(lua_State *L, const char *method) {
  if (method == NULL) method = "new";
  lua_pushstring(L, method);
  lua_pushcclosure(L, Llibcall, 1);
  return lbind_setmetafield(L, -2, "__call");
}

static int call_accessor(lua_State *L, int idx, int nargs) {
  lua_CFunction f = lua_tocfunction(L, idx);
  if (f != NULL) {
    lua_settop(L, nargs);
    return f(L); 
  }
  return -1;
}

static int call_lut(lua_State *L, int idx, int nargs) {
  lua_CFunction f = lua_tocfunction(L, idx);
  /* look up table */
  if (f == NULL) {
    lua_pushvalue(L, 2);
    lua_rawget(L, relate_index(idx, 1));
    f = lua_tocfunction(L, -1);
  }
  if (f != NULL) {
    lua_settop(L, nargs);
    return f(L); 
  }
  return -1;
}

static int Lnewindex(lua_State *L) {
  int nret;
  /* upvalue: seti, seth */
  if (!lua_isnone(L, lua_upvalueindex(1)) &&
      (nret = call_accessor(L, lua_upvalueindex(1), 3)) >= 0)
    return nret;
  if (!lua_isnone(L, lua_upvalueindex(2)) &&
      (nret = call_lut(L, lua_upvalueindex(2), 3)) >= 0)
    return nret;
  /* set it in uservalue table */
  if (lua_isuserdata(L, 1)) {
    lua_getuservalue(L, 1);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_setuservalue(L, 1);
    }
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_rawset(L, -3);
  }
  return 0;
}

static int Lindex(lua_State *L) {
  int i, nret;
  /* upvalue: seti, seth, tables */
  if (lua_isuserdata(L, 1)) {
    lua_getuservalue(L, 1);
    if (!lua_isnil(L, -1)) {
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      if (!lua_isnil(L, -1))
        return 1;
    }
  }
  if (lua_getmetatable(L, 1)) {
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1))
      return 1;
  }
  if (!lua_isnone(L, lua_upvalueindex(1)) &&
      (nret = call_accessor(L, lua_upvalueindex(1), 2)) >= 0)
    return nret;
  if (!lua_isnone(L, lua_upvalueindex(2)) &&
      (nret = call_lut(L, lua_upvalueindex(2), 2)) >= 0)
    return nret;
  /* find in libtable/superlibtable */
  for (i = 3; !lua_isnone(L, lua_upvalueindex(i)); ++i) {
    lua_settop(L, 2);
    if (lua_islightuserdata(L, lua_upvalueindex(i))) {
      if (!lbind_getmetatable(L, lua_touserdata(L, lua_upvalueindex(i))))
        continue;
      lua_replace(L, lua_upvalueindex(i));
    }
    lua_pushvalue(L, 2);
    lua_gettable(L, lua_upvalueindex(i));
    if (!lua_isnil(L, -1))
      return 1;
  }
  return 0;
}

static void push_indexf(lua_State *L, int ntables) {
  lua_pushnil(L);
  lua_insert(L, -ntables-1);
  lua_pushnil(L);
  lua_insert(L, -ntables-1);
  lua_pushcclosure(L, Lindex, ntables+2);
}

static void push_newindexf(lua_State *L) {
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushcclosure(L, Lnewindex, 2);
}

void lbind_indexf(lua_State *L, int ntables) {
  push_indexf(L, ntables);
  lua_setfield(L, -2, "__index");
}

void lbind_newindexf(lua_State *L) {
  push_newindexf(L);
  lua_setfield(L, -2, "__newindex");
}

static void get_default_metafield(lua_State *L, int idx, int field) {
  if (field == LBIND_INDEX) {
    lua_getfield(L, idx, "__index");
    if (lua_isnil(L, -1) || lua_tocfunction(L, -1) != Lindex) {
      lua_pop(L, 1);
      push_indexf(L, 0);
      lua_pushvalue(L, -1);
      lua_setfield(L, relate_index(idx, 2), "__index");
    }
  }
  else if (field == LBIND_NEWINDEX) {
    lua_getfield(L, idx, "__newindex");
    if (lua_isnil(L, -1) || lua_tocfunction(L, -1) != Lnewindex) {
      lua_pop(L, 1);
      push_newindexf(L);
      lua_pushvalue(L, -1);
      lua_setfield(L, relate_index(idx, 2), "__newindex");
    }
  }
}

static void set_cfuncupvalue(lua_State *L, lua_CFunction f, int field, int idx) {
  if ((field & LBIND_INDEX) != 0) {
    get_default_metafield(L, -1, LBIND_INDEX);
    lua_pushcfunction(L, f);
    lua_setupvalue(L, -2, idx);
    lua_pop(L, 1);
  }
  if ((field & LBIND_NEWINDEX) != 0) {
    get_default_metafield(L, -1, LBIND_NEWINDEX);
    lua_pushcfunction(L, f);
    lua_setupvalue(L, -2, idx);
    lua_pop(L, 1);
  }
}

void lbind_setarrayf(lua_State *L, lua_CFunction f, int field) {
  set_cfuncupvalue(L, f, field, 1);
}

void lbind_sethashf(lua_State *L, lua_CFunction f, int field) {
  set_cfuncupvalue(L, f, field, 2);
}

void lbind_setmaptable(lua_State *L, luaL_Reg libs[], int field) {
  lua_newtable(L);
  luaL_setfuncs(L, libs, 0);
  if ((field & LBIND_INDEX) != 0) {
    get_default_metafield(L, -2, LBIND_INDEX);
    lua_pushvalue(L, -2);
    lua_setupvalue(L, -2, 2);
    lua_pop(L, 1);
  }
  if ((field & LBIND_NEWINDEX) != 0) {
    get_default_metafield(L, -2, LBIND_NEWINDEX);
    lua_pushvalue(L, -2);
    lua_setupvalue(L, -2, 2);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
}


/* lbind userdata maintain */

typedef union {
  lbind_MaxAlign dummy; /* ensures maximum alignment for `intern' object */
  struct {
    void *instance;
    int flags;
  } o;
} lbind_Object;

#define check_size(L,n) (lua_rawlen((L),(n)) >= sizeof(lbind_Object))

int lbind_retrieve(lua_State *L, const void *p) {
  get_pointerbox(L); /* 1 */
  lua_rawgetp(L, -1, p); /* 2 */
  if (lua_isnil(L, -1)) {
    lua_pop(L, 2);
    return 0;
  }
  lua_remove(L, -2);
  return 1;
}

static void intentobj(lua_State *L, lbind_Object *obj) {
  /* stack: object */
  get_pointerbox(L);
  lua_pushvalue(L, -2);
  lua_rawsetp(L, -2, obj->o.instance);
  lua_pop(L, 1);
}

static lbind_Object *newobj(lua_State *L, size_t objsize, int flags) {
  lbind_Object *obj;
  obj = (lbind_Object*)lua_newuserdata(L, sizeof(lbind_Object) + objsize);
  obj->o.flags = flags;
  obj->o.instance = (void*)(obj+1);
  if (objsize != 0 && (flags & LBIND_INTENT) != 0)
    intentobj(L, obj);
  return obj;
}

static lbind_Object *testobj(lua_State *L, int idx) {
  lbind_Object *obj = (lbind_Object*)lua_touserdata(L, idx);
  if (obj != NULL) {
    if (!check_size(L, idx) || obj->o.instance == NULL)
      obj = NULL;
    else {
#if 0
      get_pointerbox(L); /* 1 */
      lua_rawgetp(L, -1, obj->o.instance); /* 2 */
      if (!lua_rawequal(L, relate_index(idx, 2), -1))
        obj = NULL;
      lua_pop(L, 2); /* (2)(1) */
#endif
    }
  }
  return obj;
}

void *lbind_new(lua_State *L, size_t objsize, const lbind_Type *t) {
  lbind_Object *obj = newobj(L, objsize, t->flags);
  if (lbind_getmetatable(L, t))
    lua_setmetatable(L, -2);
  return obj->o.instance;
}

void *lbind_raw(lua_State *L, size_t objsize) {
  return newobj(L, objsize, 0)->o.instance;
}

void *lbind_object(lua_State *L, int idx) {
  lbind_Object *obj = testobj(L, idx);
  return obj == NULL ? NULL : obj->o.instance;
}

void lbind_track(lua_State *L, int idx) {
  lbind_Object *obj = testobj(L, idx);
  if (obj != NULL)
    obj->o.flags |= LBIND_GC;
}

void lbind_untrack(lua_State *L, int idx) {
  lbind_Object *obj = testobj(L, idx);
  if (obj != NULL)
    obj->o.flags &= ~LBIND_GC;
}

int lbind_hastrack(lua_State *L, int idx) {
  lbind_Object *obj = testobj(L, idx);
  return obj != NULL && (obj->o.flags & LBIND_GC) != 0;
}

void lbind_register(lua_State *L, const void *p, const lbind_Type *t) {
  lbind_Object *obj = newobj(L, 0, t->flags);
  obj->o.instance = (void*)p;
  if ((obj->o.flags & LBIND_INTENT) != 0)
    intentobj(L, obj);
  if (lbind_getmetatable(L, t))
    lua_setmetatable(L, -2);
}

void *lbind_unregister(lua_State *L, int idx) {
  void *u = NULL;
  lbind_Object *obj = (lbind_Object*)lua_touserdata(L, idx);
  if (obj != NULL) {
    if (!check_size(L, idx))
      return NULL;
    if ((u = obj->o.instance) != NULL) {
      obj->o.instance = NULL;
      obj->o.flags &= ~LBIND_GC;
#if LUA_VERSION_NUM < 502
      get_pointerbox(L); /* 1 */
      lua_pushnil(L); /* 2 */
      lua_rawsetp(L, -3, u); /* 2->1 */
      lua_pop(L, 1); /* (1) */
#endif
    }
  }
  return u;
}


/* lbind type registry */

LB_API void lbind_inittype(lbind_Type *t, const char *name) {
  t->sign = LBIND_TYPE_SIGN;
  t->name = name;
  t->flags = LBIND_DEFFLAG;
  t->cast = NULL;
  t->bases = NULL;
}

LB_API void lbind_setbase(lbind_Type *t, lbind_Type **bases, lbind_Cast *cast) {
  t->bases = bases;
  t->cast = cast;
}

LB_API int lbind_setautotrack(lbind_Type *t, int autotrack) {
  int old_flag = t->flags&LBIND_GC ? 1 : 0;
  if (autotrack)
    t->flags |= LBIND_GC;
  else
    t->flags &= ~LBIND_GC;
  return old_flag;
}

lbind_Type *lbind_typeobject(lua_State *L, int idx) {
  lbind_Type *t = NULL;
  if (lua_getmetatable(L, idx)) {
    lua_getfield(L, -1, "__type");
    t = (lbind_Type*)lua_touserdata(L, -1);
    lua_pop(L, 2);
    if (t != NULL)
      return t;
  }
  if (lua_istable(L, idx)) {
    lua_getfield(L, idx, "__type");
    t = (lbind_Type*)lua_touserdata(L, -1);
    lua_pop(L, 1);
  }
  return t;
}


/* lbind type metatable */

static int Ltostring(lua_State *L) {
  lbind_tolstring(L, 1, NULL);
  return 1;
}

static int Lgc(lua_State *L) {
  lbind_Object *obj = (lbind_Object*)lua_touserdata(L, 1);
  if (obj != NULL && check_size(L, 1)) {
    if ((obj->o.flags & LBIND_GC) != 0) {
      lua_getfield(L, 1, "delete");
      if (!lua_isnil(L, -1)) {
        lua_pushvalue(L, 1);
        lua_call(L, 1, 0);
      }
      if ((obj->o.flags & LBIND_GC) != 0)
        lbind_unregister(L, 1);
    }
  }
  return 0;
}

static void register_type(lua_State *L, const char *name, const void *t) {
  /* stack: metatable */
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, name);
  lua_pushvalue(L, -1);
  lua_rawsetp(L, LUA_REGISTRYINDEX, t);

  get_typebox(L); /* 1 */
  lua_pushstring(L, name); /* 2 */
  lua_pushvalue(L, -3); /* metatable->3 */
  lua_rawset(L, -3); /* 2,3->1 */
  lua_pop(L, 1); /* (1) */
}

static int check_type(lua_State *L, const lbind_Type *t) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, (const void*)t);
  if (!lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }

  lua_getfield(L, LUA_REGISTRYINDEX, t->name);
  if (!lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_pop(L, 2);
  return 1;
}

int lbind_newmetatable(lua_State *L, luaL_Reg *libs, const lbind_Type *t) {
  if (!check_type(L, t)) return 0;

  lua_createtable(L, 0, 8);
  if (libs != NULL)
    luaL_setfuncs(L, libs, 0);

  /* init type metatable */
  lua_pushlightuserdata(L, (void*)t);
  lua_setfield(L, -2, "__type");

  if (!lbind_hasfield(L, -1, "__gc")) {
    lua_pushcfunction(L, Lgc);
    lua_setfield(L, -2, "__gc");
  }

  if (!lbind_hasfield(L, -1, "__tostring")) {
    lua_pushcfunction(L, Ltostring);
    lua_setfield(L, -2, "__tostring");
  }

  if (t->bases != NULL && t->bases[0] != NULL) {
    int nups = 0; /* stack: metatable */
    int freeslots = 0;
    lbind_Type **bases = t->bases;
    for (; *bases != NULL; ++nups, ++bases) {
      if (nups > freeslots) {
        luaL_checkstack(L, 10, "no space for base types");
        freeslots += 10;
      }
      if (!lbind_getmetatable(L, *bases))
        lua_pushlightuserdata(L, *bases);
    }
    lbind_indexf(L, nups);
  }

  else if (!lbind_hasfield(L, -1, "__index")) {
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
  }

  register_type(L, t->name, (const void*)t);
  return 1;
}

static int Lagency(lua_State *L) {
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_gettable(L, 1);
  lua_insert(L, 1);
  lua_call(L, lua_gettop(L)-1, LUA_MULTRET);
  return lua_gettop(L);
}

static void set_meta_agency(lua_State *L, const char *key) {
  /* stack: libtable mt */
  lua_pushstring(L, key);
  lua_rawget(L, -3); /* do not track __index */
  if (!lua_isnil(L, -1)) {
    lua_pushfstring(L, "__%s", key);
    lua_pushstring(L, key);
    lua_pushcclosure(L, Lagency, 1);
    lua_rawset(L, -4);
  }
  lua_pop(L, 1);
}

void lbind_setagency(lua_State *L) {
  set_meta_agency(L, "len");
#if LUA_VERSION_NUM >= 502
  set_meta_agency(L, "pairs");
  set_meta_agency(L, "ipairs");
#endif /* LUA_VERSION_NUM >= 502 */
}


/* lbind type system */

const char *lbind_tolstring(lua_State *L, int idx, size_t *plen) {
  const char *tname = lbind_type(L, idx);
  lbind_Object *obj = testobj(L, idx);
  if (obj != NULL && tname)
    lua_pushfstring(L, "%s: %p", tname, obj->o.instance);
  else if (obj == NULL) {
    lbind_Object *obj = (lbind_Object*)lua_touserdata(L, idx);
    if (obj == NULL)
      return luaL_tolstring(L, idx, plen);
    if (check_size(L, 1))
      lua_pushfstring(L, "%s[N]: %p", tname, obj->o.instance);
    else
      lua_pushfstring(L, "userdata: %p", (void*)obj);
  }
  return lua_tolstring(L, -1, plen);
}

const char *lbind_type(lua_State *L, int idx) {
  lbind_Type *t = lbind_typeobject(L, idx);
  if (t != NULL) return t->name;
  return NULL;
}

static int testtype(lua_State *L, int idx, const lbind_Type *t) {
  if (lua_getmetatable(L, idx)) { /* does it have a metatable? */
    int res = 1;
    if (!lbind_getmetatable(L, t)) { /* get correct metatable */
      lua_pop(L, 1); /* no such metatable? fail */
      return 0;
    }
    if (!lua_rawequal(L, -1, -2)) /* not the same? */
      res = 0;  /* value is a userdata with wrong metatable */
    lua_pop(L, 2);
    return res;
  }
  lua_pop(L, 1);
  return 0;
}

void *try_cast(lua_State *L, int idx, const lbind_Type *t) {
  lbind_Type *from_type = lbind_typeobject(L, idx);
  if (from_type != NULL && from_type->cast != NULL)
      return from_type->cast(L, idx, t);
  return NULL;
}

int lbind_isa(lua_State *L, int idx, const lbind_Type *t) {
  return testtype(L, idx, t) || try_cast(L, idx, t) != NULL;
}

void *lbind_cast(lua_State *L, int idx, const lbind_Type *t) {
  lbind_Object *obj = (lbind_Object*)lua_touserdata(L, idx);
  if (!check_size(L, idx) || obj == NULL || obj->o.instance == NULL)
    return NULL;
  return testtype(L, idx, t) ? obj->o.instance : try_cast(L, idx, t);
}

void lbind_copy(lua_State *L, const void *obj, const lbind_Type *t) {
  if (lbind_getmetatable(L, t)) {
    lua_pushliteral(L, "new");
    lua_rawget(L, -2);
  }
  if (lua_isnil(L, -1)) {
    lua_pop(L, 2);
    lua_pushnil(L);
  }
  else {
    lua_remove(L, -2);
    lbind_register(L, obj, t);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
      lua_pop(L, 1);
      lua_pushnil(L);
    }
    lbind_track(L, -1); /* enable autodeletion for copied stuff */
  }
}

void *lbind_check(lua_State *L, int idx, const lbind_Type *t) {
  lbind_Object *obj = (lbind_Object*)lua_touserdata(L, idx);
  void *u = NULL;
  if (!check_size(L, idx))
    luaL_argerror(L, idx, "invalid lbind userdata");
  if (obj != NULL && obj->o.instance == NULL)
    luaL_argerror(L, idx, "null lbind object");
  u = testtype(L, idx, t) ? obj->o.instance : try_cast(L, idx, t);
  if (u == NULL)
    lbind_typeerror(L, idx, t->name);
  return u;
}

void *lbind_test(lua_State *L, int idx, const lbind_Type *t) {
  lbind_Object *obj = (lbind_Object*)lua_touserdata(L, idx);
  return testtype(L, idx, t) ? obj->o.instance : try_cast(L, idx, t);
}


#ifndef LBIND_NO_ENUM
/* parse string enumerate value */

static const char *mask = 
  "         ss  s                  "
  "s           s    wwwwwwwwww     "
  " wwwwwwwwwwwwwwwwwwwwwwwwww    w"
  " wwwwwwwwwwwwwwwwwwwwwwwwww s   ";

static const char *skip_white(const char *s) {
  int ch;
  while ((ch = (*s & 0xFF)) < 0x7F && mask[ch] == 's')
    ++s;
  return s;
}

static const char *skip_ident(const char *s) {
  int ch;
  if ((ch = (*s & 0xFF)) >= 0x7F || mask[ch] != 'w')
    return s;
  while ((ch = (*s & 0xFF)) < 0x7F && mask[ch] == 'w')
    ++s;
  return s;
}

static int parse_ident(lua_State *L, const char *s, const char *e, int *value) {
  int valid;
  lua_pushlstring(L, s, e-s);
  *value = lua_tointegerx(L, -1, &valid);
  if (!valid) {
    lua_rawget(L, -2);
    *value = lua_tointegerx(L, -1, &valid);
  }
  lua_pop(L, 1);
  if (!valid) {
    lua_pushlstring(L, s, e - s);
    lua_pushfstring(L, "unexpected ident " LUA_QS " in enum",
        lua_tostring(L, -1));
    lua_remove(L, -2);
    return 0;
  }
  return 1;
}

static int parse_mask(lua_State *L, const char *s, int *penum, int check) {
  *penum = 0;
  while (*s != '\0') {
    int evalue;
    int inversion = 0;
    s = skip_white(s);
    if (*s == '~') {
      ++s;
      inversion = 1;
      s = skip_white(s);
    }
    if (*s != '\0') {
      const char *e = skip_ident(s);
      if (e == s)
        lua_pushfstring(L, "unexpected token '%c' in enum", *s);
      if (!parse_ident(L, s, e, &evalue)) {
        if (!check) {
          lua_pop(L, 1);
          return 0;
        }
        else return lua_error(L);
      }
      if (inversion)
        *penum &= ~evalue;
      else
        *penum |= evalue;
    }
  }
  return 1;
}


/* lbind enum registry */

static void add_enums(lua_State *L, lbind_Enum *et, lbind_EnumItem enums[]) {
  /* stack: enumtable */
  for (; enums->name != NULL; ++enums) {
    lua_pushstring(L, enums->name); /* 1 */
    lua_pushinteger(L, enums->value); /* 2 */
    lua_rawset(L, -3); /* 1,2->etable */
    if (et->lastn < enums->value)
      et->lastn = enums->value;
  }
}

static int Lenumcall(lua_State *L) {
  lbind_Enum *t = (lbind_Enum*)lbind_typeobject(L, 1);
  if (t->sign != LBIND_ENUM_SIGN)
    return lbind_typeerror(L, 1, "enum");
  lua_pushinteger(L, lbind_checkmask(L, 2, t));
  return 1;
}

void lbind_initenum(lbind_Enum *et, const char *name) {
  et->sign = LBIND_ENUM_SIGN;
  et->name = name;
  et->lastn = 0;
  et->enums = NULL;
}

int lbind_newenumtable(lua_State *L, lbind_Enum *et, lbind_EnumItem *enums) {
  if (!check_type(L, (lbind_Type*)et)) return 0;
  et->enums = enums;
  lua_createtable(L, 0, 8);
  add_enums(L, et, enums);
  lua_pushcfunction(L, Lenumcall);
  lbind_setmetafield(L, -2, "__call");
  register_type(L, et->name, (const void*)et);
  return 1;
}

int lbind_addenums(lua_State *L, lbind_EnumItem *enums, lbind_Enum *et) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, et);
  add_enums(L, et, enums);
  lua_pop(L, 1);
  return et->lastn;
}


/* lbind enum type system */

static int toenum(lua_State *L, int idx, lbind_Enum *et, int mask, int check) {
  const char *str;
  int value = lua_tointeger(L, idx);
  if (value != 0 || lua_isnumber(L, idx))
    return value;
  if ((str = lua_tostring(L, idx)) != NULL) {
    int success;
    lua_rawgetp(L, LUA_REGISTRYINDEX, et); /* 1 */
    if (mask)
      success = parse_mask(L, str, &value, check);
    else {
      lua_pushvalue(L, relate_index(idx, 1)); /* idx->2 */
      lua_rawget(L, -2); /* 2->2 */
      value = lua_tointegerx(L, -1, &success);
      lua_pop(L, 1); /* (2) */
      if (check && !success)
        return luaL_error(L, "'%s' is not valid %s", str, et->name);
    }
    lua_pop(L, 1); /* (1) */
    if (success)
      return value;
  }
  if (check)
    lbind_typeerror(L, idx, et->name);
  return -1;
}

int lbind_pushmask(lua_State *L, int evalue, lbind_Enum *et) {
  lua_pushinteger(L, evalue);
  return evalue;
}

int lbind_pushenum(lua_State *L, const char *name, lbind_Enum *et) {
  int res, success;
  lua_rawgetp(L, LUA_REGISTRYINDEX, et); /* 1 */
  lua_pushstring(L, name); /* 2 */
  lua_pushvalue(L, -1); /* 2->3 */
  lua_rawget(L, -3); /* 3->3 */
  lua_remove(L, -2); /* (1) */
  res = lua_tointegerx(L, -1, &success);
  if (!success) {
    lua_pop(L, 1); /* (3) */
    return -1;
  }
  lua_remove(L, -2); /* (2) */
  return res;
}

int lbind_testmask(lua_State *L, int idx, lbind_Enum *et) {
  return toenum(L, idx, et, 1, 0);
}

int lbind_checkmask(lua_State *L, int idx, lbind_Enum *et) {
  return toenum(L, idx, et, 1, 1);
}

int lbind_testenum(lua_State *L, int idx, lbind_Enum *et) {
  return toenum(L, idx, et, 0, 0);
}

int lbind_checkenum(lua_State *L, int idx, lbind_Enum *et) {
  return toenum(L, idx, et, 0, 1);
}
#endif /* LBIND_NO_ENUM */


#ifndef LBIND_NO_RUNTIME
/* lbind Lua side runtime */

static lbind_Type *test_type(lua_State *L, int idx) {
  lbind_Type *t = (lbind_Type*)lua_touserdata(L, idx);
  unsigned sign;
  if (t == NULL
      || ((sign = *(unsigned*)t) != LBIND_TYPE_SIGN
        || sign != LBIND_ENUM_SIGN))
    return lbind_typeobject(L, -1);
  return t;
}

static int Lregister(lua_State *L) {
  int i, top = lua_gettop(L);
  for (i = 1; i <= top; ++i)
    lbind_track(L, i);
  return top;
}

static int Lunregister(lua_State *L) {
  int i, top = lua_gettop(L);
  for (i = 1; i <= top; ++i)
    lbind_untrack(L, i);
  return top;
}

static int Lowner(lua_State *L) {
  int i, top = lua_gettop(L);
  luaL_checkstack(L, top, "no space for owner info");
  for (i = 1; i <= top; ++i) {
    if (lbind_hastrack(L, i))
      lua_pushliteral(L, "Lua");
    else
      lua_pushliteral(L, "C");
  }
  return top;
}

static int Lnull(lua_State *L) {
  int i, top = lua_gettop(L);
  for (i = 1; i <= top; ++i) {
    const void *u = lbind_object(L, i);
    if (u != NULL) {
      lua_pushnil(L);
      lua_replace(L, i);
    }
  }
  return top;
}

static int Lvalid(lua_State *L) {
  const void *u = lua_touserdata(L, -1);
  if (u != NULL) {
    get_pointerbox(L);
    lua_rawgetp(L, -1, u);
  }
  else
    lua_pushnil(L);
  return 1;
}

static int Ldelete(lua_State *L) {
  lua_getfield(L, -1, "delete");
  if (!lua_isnil(L, -1)) {
    lua_pushvalue(L, -2);
    lua_call(L, 1, 0);
  }
  return 0;
}

static int Linfo(lua_State *L) {
#define INFOS(X) \
  X("pointers",     get_pointerbox(L)) \
  X("types",        get_typebox(L))    \

  size_t len;
  const char *s = luaL_checklstring(L, 1, &len);
#define X(opt,cmd)               \
  if (strncmp(s, opt, len) == 0) \
  { cmd; return 1; }
  INFOS(X)
#undef  X
  return luaL_argerror(L, 1, "invalid option");
#undef INFOS
}

static int Lisa(lua_State *L) {
  lbind_Type *t = test_type(L, -1);
  if (t != NULL) lua_pushboolean(L, lbind_isa(L, -2, t));
  return 1;
}

static int Lcast(lua_State *L) {
  lbind_Type *t = test_type(L, -1);
  void *u;
  if (t != NULL && (u = lbind_cast(L, -2, t)) != NULL) {
    lbind_register(L, u, t);
    return 1;
  }
  return 0;
}

static int Ltype(lua_State *L) {
  lbind_Type *t = test_type(L, -1);
  if (t == NULL) {
    lua_pushstring(L, luaL_typename(L, -1));
    return 1;
  }
  lua_pushstring(L, t->name);
  lua_pushstring(L, t->sign == LBIND_TYPE_SIGN ? "type" : "enum");
  return 2;
}

static int Lbases(lua_State *L) {
  int i = 1;
  lbind_Type **bases, *t = test_type(L, 1);
  if (t == NULL)
    return lbind_typeerror(L, 1, "type");
  bases = t->bases;
  lua_settop(L, 2);
  if (!lua_istable(L, 2)) {
    lua_newtable(L);
    lua_replace(L, 2);
  }
  for (; *bases != NULL; ++bases) {
    if (!lbind_getmetatable(L, *bases))
      lua_pushnil(L);
    lua_rawseti(L, -2, i);
  }
  lua_pushinteger(L, i);
  lua_setfield(L, -2, "n");
  return 1;
}

static int Lmask(lua_State *L) {
  luaL_Buffer b;
  lbind_Enum *et = (lbind_Enum*)test_type(L, 1);
  lbind_EnumItem *ei;
  int first = 1;
  unsigned value = lbind_checkmask(L, 2, et);
  if (et == NULL || et->sign != LBIND_ENUM_SIGN)
    lbind_typeerror(L, 1, "enum");
  if (et->enums == NULL) {
    lua_pushliteral(L, "");
    return 0;
  }
  luaL_buffinit(L, &b);
  for (ei = et->enums; ei->name != NULL; ++ei) {
    if ((ei->value & value) == value) {
      if (first)
        first = 0;
      else
        luaL_addchar(&b, ' ');
      luaL_addstring(&b, ei->name);
      value &= ~ei->value;
    }
  }
  luaL_pushresult(&b);
  return 1;
}

int luaopen_lbind(lua_State *L) {
  luaL_Reg libs[] = {
#define ENTRY(name) { #name, L##name }
    ENTRY(bases),
    ENTRY(cast),
    ENTRY(delete),
    ENTRY(info),
    ENTRY(isa),
    ENTRY(mask),
    ENTRY(null),
    ENTRY(owner),
    ENTRY(register),
    ENTRY(type),
    ENTRY(unregister),
    ENTRY(valid),
#undef ENTRY
    { NULL, NULL }
  };

  luaL_newlib(L, libs);
#if LUA_VERSION_NUM < 502
  lua_pushvalue(L, -1);
  lua_setglobal(L, "lbind");
#endif
  return 1;
}
#endif /* LBIND_NO_RUNTIME */

/*
 * cc: lua='lua52' flags+='-s -O2 -Wall -pedantic -mdll -Id:/$lua/include'
 * cc: flags+='-DLUA_BUILD_AS_DLL' output='lbind.dll'
 * cc: run='$lua tt.lua' libs+='d:/$lua/$lua.dll'
 */
