
#include "lua.h"


// These 2 macros helps us in constructing general metamethods.
// We can use "lua" as a "Lua" pointer and arg1, arg2, ..., arg5 as Variants objects
// Check examples in createVector2Metatable
#define LUA_LAMBDA_TEMPLATE(_f_) \
 [](lua_State* inner_state) -> int {                      \
     lua_pushstring(inner_state,"__Lua");                 \
     lua_rawget(inner_state,LUA_REGISTRYINDEX);           \
     Lua* lua = (Lua*) lua_touserdata(inner_state,-1);;   \
     lua_pop(inner_state,1);                              \
     Variant arg1 = lua->getVariant(1);          					\
     Variant arg2 = lua->getVariant(2);          					\
     Variant arg3 = lua->getVariant(3);          					\
     Variant arg4 = lua->getVariant(4);          					\
     Variant arg5 = lua->getVariant(5);          					\
     _f_                                         					\
}
#define LUA_METAMETHOD_TEMPLATE( lua_state , metatable_index , metamethod_name , _f_ )\
lua_pushstring(lua_state,metamethod_name); \
lua_pushcfunction(lua_state,LUA_LAMBDA_TEMPLATE( _f_ )); \
lua_settable(lua_state,metatable_index-2);

Lua::Lua(){
	// Createing lua state instance
	state = luaL_newstate();
	threaded = true;
	
	// loading base libs
    	luaL_requiref(state, "", luaopen_base, 1);
   	lua_pop(state, 1);
    	luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1);
    	lua_pop(state, 1);
    	luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1);
    	lua_pop(state, 1);
    	luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1);
    	lua_pop(state, 1);
	
	lua_sethook(state, &LineHook, LUA_MASKLINE, 0);
	lua_register(state, "print", luaPrint);

	// saving this object into registry
	lua_pushstring(state,"__Lua");
	lua_pushlightuserdata( state , this );
	lua_rawset( state , LUA_REGISTRYINDEX );

	// Creating basic types metatables and saving them in registry
	createVector2Metatable(state); // "mt_Vector2"
	createVector3Metatable(state); // "mt_Vector3"
	createColorMetatable(state); // "mt_Color"

	// Exposing basic types constructors
	exposeConstructors(state);
	
}

Lua::~Lua(){
    // Destroying lua state instance
    lua_close(state);
}

static bool shouldKill = false;
// Bind C++ functions to GDScript
void Lua::_bind_methods(){
    ClassDB::bind_method(D_METHOD("killAll"),&Lua::killAll);
    ClassDB::bind_method(D_METHOD("setThreaded", "bool"),&Lua::setThreaded);
    ClassDB::bind_method(D_METHOD("doFile", "NodeObject", "File", "Callback=String()"), &Lua::doFile);
    ClassDB::bind_method(D_METHOD("doString", "NodeObject", "Code", "Callback=String()"), &Lua::doString);
    ClassDB::bind_method(D_METHOD("pushVariant", "var"),&Lua::pushGlobalVariant);
    ClassDB::bind_method(D_METHOD("exposeFunction", "NodeObject", "GDFunction", "LuaFunctionName"),&Lua::exposeFunction);
    ClassDB::bind_method(D_METHOD("callFunction", "NodeObject", "LuaFunctionName", "Args"), &Lua::callFunction);
}

// expose a GDScript function to lua
void Lua::exposeFunction(Object *instance, String function, String name){
  
  // Createing lamda function so we can capture the object instanse and call the GDScript method. Or in theory other scripting languages?
  auto f = [](lua_State* L) -> int{
    const Object *instance2 = (const Object*) lua_topointer(L, lua_upvalueindex(1));
    class Lua *obj = (class Lua*) lua_topointer(L, lua_upvalueindex(2));
    const char *function2 = lua_tostring(L, lua_upvalueindex(3));

    Variant arg1 = obj->getVariant(1);
    Variant arg2 = obj->getVariant(2);
    Variant arg3 = obj->getVariant(3);
    Variant arg4 = obj->getVariant(4);
    Variant arg5 = obj->getVariant(5);

    ScriptInstance *scriptInstance = instance2->get_script_instance();
    Variant returned = scriptInstance->call(function2, arg1, arg2, arg3, arg4, arg5);

    // Always returns something. If script instance doesn't returns anything, it will returns a NIL value anyway
    obj->pushVariant(returned);
    return 1;

  };

  // Pushing the object instnace to the stack to be retrived when the function is called
  lua_pushlightuserdata(state, instance);

  // Pushing the referance of the class
  lua_pushlightuserdata(state, this);

  // Convert lua and gdscript function names from wstring to string for lua's usage
  std::wstring temp = function.c_str();
  std::string func(temp.begin(), temp.end());

  temp = name.c_str();
  std::string fname(temp.begin(), temp.end());

  // Pushing the script function name string to the stack to br retrived when called
  lua_pushstring(state, func.c_str());

  // Pushing the actual lambda function to the stack
  lua_pushcclosure(state, f, 3);
  // Setting the global name for the function in lua
  lua_setglobal(state, fname.c_str());
  
}

// call a Lua function from GDScript
void Lua::callFunction(Object *instance, String name, Array args) {
    std::wstring temp = name.c_str();
    std::string fname(temp.begin(), temp.end());

    // put global function name on stack
    lua_getglobal(state, fname.c_str());

    // push args
    for (int i = 0; i < args.size(); ++i) {
        pushVariant(args[i]);
    }

    // call function (for now, lua functions cannot return values to gdscript)
    lua_call(state, args.size(), 0);
}

void Lua::setThreaded(bool thread){
    threaded = thread;
}

// doFile() will just load the file's text and call doString()
void Lua::doFile(Object *instance, String fileName, String callback){
  _File file;

  file.open(fileName,_File::ModeFlags::READ);
  String code = file.get_as_text();
  file.close();

  doString(instance, code, callback);
}

// kill all active threads
void Lua::killAll(){
    //TODO: Create a method to kill individual threads
    shouldKill = true;
}

// Called every line of lua that is ran
void Lua::LineHook(lua_State *L, lua_Debug *ar){
    if(shouldKill){
        luaL_error(L, "Execution terminated");
        shouldKill = false;
    }
}

// Run lua string in a thread if threading is enabled
void Lua::doString(Object *instance, String code, String callback){
    if(threaded){
        std::thread(runLua, instance, code, callback, state).detach();
    }else{
        runLua(instance, code, callback, state);
    }
}

// Execute a lua script string and call the passed callBack function with the error as the aurgument if an error occures
void Lua::runLua(Object *instance, String code, String callback, lua_State *L){
    std::wstring luaCode = code.c_str();

    int result = luaL_dostring(L, std::string( luaCode.begin(), luaCode.end() ).c_str());
    if(result != LUA_OK){
        if (callback != String()){
            ScriptInstance *scriptInstance = instance->get_script_instance();
            scriptInstance->call(callback, lua_tostring(L, -1));
        }
    }
}

// Push a GD Variant to the lua stack and return false if type is not supported (in this case, returns a nil value).
bool Lua::pushVariant(Variant var) {
    std::wstring str;
    switch (var.get_type())
    {
        case Variant::Type::NIL:
            lua_pushnil(state);
            break;
        case Variant::Type::STRING:
            str = (var.operator String().c_str());
            lua_pushstring(state, std::string( str.begin(), str.end() ).c_str());
            break;
        case Variant::Type::INT:
            lua_pushinteger(state, (int64_t)var);
            break;
        case Variant::Type::REAL:
            lua_pushnumber(state,var.operator double());
            break;
        case Variant::Type::BOOL:
            lua_pushboolean(state, (bool)var);
            break;
        case Variant::Type::POOL_BYTE_ARRAY:
        case Variant::Type::POOL_INT_ARRAY:
        case Variant::Type::POOL_STRING_ARRAY:
        case Variant::Type::POOL_REAL_ARRAY:
        case Variant::Type::POOL_VECTOR2_ARRAY:
        case Variant::Type::POOL_VECTOR3_ARRAY:
        case Variant::Type::POOL_COLOR_ARRAY:            
        case Variant::Type::ARRAY: {
            Array array = var.operator Array();
            lua_newtable(state);
            for(int i = 0; i < array.size(); i++) {
                Variant key = i+1;
                Variant value = array[i];
                pushVariant(key);
                pushVariant(value);
                lua_settable(state,-3);
            }
            break;
        }
        case Variant::Type::DICTIONARY:
            lua_newtable(state);
            for(int i = 0; i < ((Dictionary)var).size(); i++) {
                Variant key = ((Dictionary)var).keys()[i];
                Variant value = ((Dictionary)var)[key];
                pushVariant(key);
                pushVariant(value);
                lua_settable(state, -3);
            }
            break;
        case Variant::Type::VECTOR2: {
            void* userdata = (Variant*)lua_newuserdata( state , sizeof(Variant) );
            memcpy( userdata , (void*)&var , sizeof(Variant) );
            luaL_setmetatable(state,"mt_Vector2");
            break;     
        }     
        case Variant::Type::VECTOR3: {
            void* userdata = (Variant*)lua_newuserdata( state , sizeof(Variant) );
            memcpy( userdata , (void*)&var , sizeof(Variant) );
            luaL_setmetatable(state,"mt_Vector3");
            break;     
        }
        case Variant::Type::COLOR: {
            void* userdata = (Variant*)lua_newuserdata( state , sizeof(Variant) );
            memcpy( userdata , (void*)&var , sizeof(Variant) );
            luaL_setmetatable(state,"mt_Color");
            break;    
        }
        default:
            print_error( vformat("Can't pass Variants of type \"%s\" to Lua." , Variant::get_type_name( var.get_type() ) ) );
            lua_pushnil(state);
            return false;
    }
    return true;
}

// Call pushVarient() and set it to a global name
bool Lua::pushGlobalVariant(Variant var, String name) {
    if (pushVariant(var)) {
       std::wstring str = name.c_str();
        lua_setglobal(state,std::string( str.begin(), str.end() ).c_str());
        return true;
    }
    return false;
}

// Pop the value at the top of the stack and return getVarient()
Variant Lua::popVariant() {
    Variant result = getVariant();
    lua_pop(state, 1);
    return result;
}

// get a value at the given index and return as a variant
Variant Lua::getVariant(int index) {
    Variant result;
    int type = lua_type(state, index);
    switch (type) {
        case LUA_TSTRING:
            result = lua_tostring(state, index);
            break;
        case LUA_TNUMBER:
            result = lua_tonumber(state, index);
            break;
        case LUA_TBOOLEAN:
            result = (bool)lua_toboolean(state, index);
            break;
        case LUA_TUSERDATA:
            result = *(Variant*)lua_touserdata(state,index);
            break;
        case LUA_TTABLE:
        {
            Dictionary dict;
            for (lua_pushnil(state); lua_next(state, index-1); lua_pop(state, 1)) {
                Variant key = getVariant(-2);
                Variant value = getVariant(-1);
                dict[key] = value;
            }
            result = dict;
            break;
        }
        default:
            result = Variant();
    }
    return result;
}


void Lua::exposeConstructors( lua_State* state ){
	
	lua_pushcfunction(state,LUA_LAMBDA_TEMPLATE({
        int argc = lua_gettop(inner_state);
        if( argc == 0 ){
            lua->pushVariant( Vector2() );
        } else {
		    lua->pushVariant( Vector2( arg1.operator double() , arg2.operator double() ) );
        }
		return 1;
	}));
	lua_setglobal(state, "Vector2" );

	lua_pushcfunction(state,LUA_LAMBDA_TEMPLATE({
        int argc = lua_gettop(inner_state);
        if( argc == 0 ){
            lua->pushVariant( Vector3() );
        } else {
		    lua->pushVariant( Vector3( arg1.operator double() , arg2.operator double() , arg3.operator double() ) );
        }
        return 1;
	}));
	lua_setglobal(state, "Vector3" );

	lua_pushcfunction(state,LUA_LAMBDA_TEMPLATE({
        int argc = lua_gettop(inner_state);
        if( argc == 3 ){
		    lua->pushVariant( Color( arg1.operator double() , arg2.operator double() , arg3.operator double() ) );
        } else if ( argc == 4 ) {
		    lua->pushVariant( Color( arg1.operator double() , arg2.operator double() , arg3.operator double() , arg4.operator double() ) );
        } else {
            lua->pushVariant( Color() );
        }
		return 1;
	}));
	lua_setglobal(state, "Color" );
}

// Create metatable for Vector2 and saves it at LUA_REGISTRYINDEX with name "mt_Vector2"
void Lua::createVector2Metatable( lua_State* state ){
  luaL_newmetatable( state , "mt_Vector2" );

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__index" , {
		lua->pushVariant( arg1.get( arg2 ) );
		return 1;
	});

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__newindex" , {
		// We can't use arg1 here because we need to reference the userdata
		((Variant*)lua_touserdata(inner_state,1))->set( arg2 , arg3 );
		return 0;
	});	

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__add" , {
		lua->pushVariant( arg1.operator Vector2() + arg2.operator Vector2() );
    return 1;
	});

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__sub" , {
		lua->pushVariant( arg1.operator Vector2() - arg2.operator Vector2() );
    return 1;
	});

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__mul" , {
		switch( arg2.get_type() ){
			case Variant::Type::VECTOR2:
				lua->pushVariant( arg1.operator Vector2() * arg2.operator Vector2() );
				return 1;
			case Variant::Type::INT:
			case Variant::Type::REAL:
				lua->pushVariant( arg1.operator Vector2() * arg2.operator double() );
				return 1;
			default:
				return 0;
		}
	});

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__div" , {
		switch( arg2.get_type() ){
			case Variant::Type::VECTOR2:
				lua->pushVariant( arg1.operator Vector2() / arg2.operator Vector2() );
				return 1;
			case Variant::Type::INT:
			case Variant::Type::REAL:
				lua->pushVariant( arg1.operator Vector2() / arg2.operator double() );
				return 1;
			default:
				return 0;
		}
	});

  lua_pop(state,1); // Stack is now unmodified
}

// Create metatable for Vector3 and saves it at LUA_REGISTRYINDEX with name "mt_Vector3"
void Lua::createVector3Metatable( lua_State* state ){

  luaL_newmetatable( state , "mt_Vector3" );

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__index" , {
		lua->pushVariant( arg1.get( arg2 ) );
		return 1;
	});

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__newindex" , {
		// We can't use arg1 here because we need to reference the userdata
		((Variant*)lua_touserdata(inner_state,1))->set( arg2 , arg3 );
		return 0;
	});	

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__add" , {
		lua->pushVariant( arg1.operator Vector3() + arg2.operator Vector3() );
    return 1;
	});

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__sub" , {
		lua->pushVariant( arg1.operator Vector3() - arg2.operator Vector3() );
    return 1;
	});

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__mul" , {
		switch( arg2.get_type() ){
			case Variant::Type::VECTOR3:
				lua->pushVariant( arg1.operator Vector3() * arg2.operator Vector3() );
				return 1;
			case Variant::Type::INT:
			case Variant::Type::REAL:
				lua->pushVariant( arg1.operator Vector3() * arg2.operator double() );
				return 1;
			default:
				return 0;
		}
	});

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__div" , {
		switch( arg2.get_type() ){
			case Variant::Type::VECTOR3:
				lua->pushVariant( arg1.operator Vector3() / arg2.operator Vector3() );
				return 1;
			case Variant::Type::INT:
			case Variant::Type::REAL:
				lua->pushVariant( arg1.operator Vector3() / arg2.operator double() );
				return 1;
			default:
				return 0;
		}
	});

  lua_pop(state,1); // Stack is now unmodified
}

// Create metatable for Vector3 and saves it at LUA_REGISTRYINDEX with name "mt_Vector3"
void Lua::createColorMetatable( lua_State* state ){

  luaL_newmetatable( state , "mt_Color" );

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__index" , {
		lua->pushVariant( arg1.get( arg2 ) );
		return 1;
	});

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__newindex" , {
		// We can't use arg1 here because we need to reference the userdata
		((Variant*)lua_touserdata(inner_state,1))->set( arg2 , arg3 );
		return 0;
	});	

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__add" , {
		lua->pushVariant( arg1.operator Color() + arg2.operator Color() );
    return 1;
	});

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__sub" , {
		lua->pushVariant( arg1.operator Color() - arg2.operator Color() );
    return 1;
	});

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__mul" , {
		switch( arg2.get_type() ){
			case Variant::Type::COLOR:
				lua->pushVariant( arg1.operator Color() * arg2.operator Color() );
				return 1;
			case Variant::Type::INT:
			case Variant::Type::REAL:
				lua->pushVariant( arg1.operator Color() * arg2.operator double() );
				return 1;
			default:
				return 0;
		}
	});

	LUA_METAMETHOD_TEMPLATE( state , -1 , "__div" , {
		switch( arg2.get_type() ){
			case Variant::Type::COLOR:
				lua->pushVariant( arg1.operator Color() / arg2.operator Color() );
				return 1;
			case Variant::Type::INT:
			case Variant::Type::REAL:
				lua->pushVariant( arg1.operator Color() / arg2.operator double() );
				return 1;
			default:
				return 0;
		}
	});

  lua_pop(state,1); // Stack is now unmodified
}

// Lua functions
// Change lua's print function to print to the Godot console by default
int Lua::luaPrint(lua_State* state)
{

  int args = lua_gettop(state);
	String final_string;
  for ( int n=1; n<=args; ++n) {
		String it_string;
		
		switch( lua_type(state,n) ){
			case LUA_TUSERDATA:{
				Variant var = *(Variant*) lua_touserdata(state,n);
				it_string = var.operator String();
				break;
			}
			default:{
				it_string = lua_tostring(state, n);
				break;
			}
		}

		final_string += it_string;
		if( n < args ) final_string += ", ";
  }

	print_line( final_string );

  return 0;
}
