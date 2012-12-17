#ifndef VZ_Json_hpp_
#define VZ_Json_hpp_

#include <json/json.h>
#include <shared_ptr.hpp>


class Json {
public:
	typedef vz::shared_ptr<Json> Ptr;

	Json(struct json_object *jso) : _jso(jso) {};
	~Json() { _jso=NULL;};

	struct json_object* Object() { return _jso; }

private:
	struct json_object* _jso;

};


#endif /* VZ_Json_hpp_ */
