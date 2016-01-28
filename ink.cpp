#include <stdlib.h>
#include <typeinfo>
#include "core/hash.h"
#include "core/object.h"
#include "core/expression.h"
#include "core/debug.h"
#include "core/package/load.h"
#include "core/gc/collect.h"
#include "core/thread/thread.h"
#include "core/coroutine/coroutine.h"
#include "core/package/load.h"
#include "core/native/native.h"
#include "core/interface/engine.h"
#include "core/interface/setting.h"
#include "includes/universal.h"

using namespace ink;
using namespace std;

int main(int argc, char **argv)
{
	Ink_loadAllModules();
	InkActor_initActorMap();
	Ink_initNativeExpression();
	// Ink_GlobalConstantInit();

	Ink_InterpreteEngine *engine = new Ink_InterpreteEngine();
	string *tmp_name = new string(INK_ROOT_ACTOR_NAME);
	InkActor_addActor(*tmp_name, engine, pthread_self(), tmp_name, true);

	engine->startParse(Ink_InputSetting::parseArg(argc, argv));
	engine->execute();

	InkActor_setDeadActor(engine);
	InkActor_joinAllActor(engine);

	delete engine;
	cleanAll(engine);
	Ink_disposeModules();
	Ink_cleanNativeExpression();

	return 0;
}
