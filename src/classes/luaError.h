#ifndef LUAERROR_H
#define LUAERROR_H

#ifndef LAPI_GODOT_EXTENSION
#include "core/object/ref_counted.h"
#include "core/core_bind.h"
#else
#include <godot_cpp/classes/ref.hpp>
#endif

#include <lua/lua.hpp>

#ifdef LAPI_GODOT_EXTENSION
using namespace godot;
#endif

class LuaError : public RefCounted {
    GDCLASS(LuaError, RefCounted);

    protected:
        static void _bind_methods();

    public:
        enum ErrorType{
            ERR_TYPE    = 1,
            ERR_RUNTIME = LUA_ERRRUN,
            ERR_SYNTAX  = LUA_ERRSYNTAX,
            ERR_MEMORY  = LUA_ERRMEM,
            ERR_ERR     = LUA_ERRERR,
            ERR_FILE    = LUA_ERRFILE,
        };
        static LuaError* newError(String msg, ErrorType type);

        void setInfo(String msg, ErrorType type);
        bool operator==(const ErrorType type);
        bool operator==(const LuaError err);

        void setMessage(String msg);
        String getMessage() const;
        void setType(ErrorType type);
        ErrorType getType() const;

    private:
        ErrorType errType;
        String errMsg;
};

#ifndef LAPI_GODOT_EXTENSION
VARIANT_ENUM_CAST(LuaError::ErrorType)
#else
VARIANT_ENUM_CAST(LuaError, LuaError::ErrorType)
#endif

#endif