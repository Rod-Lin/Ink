#include "engine.h"

Ink_InterpreteEngine *current_interprete_engine = NULL;

void Ink_setCurrentEngine(Ink_InterpreteEngine *engine)
{
	current_interprete_engine = engine;
	return;
}

Ink_InterpreteEngine *Ink_getCurrentEngine()
{
	return current_interprete_engine;
}

void Ink_InterpreteEngine::startParse(FILE *input)
{
	Ink_InterpreteEngine *backup = Ink_getCurrentEngine();
	Ink_setCurrentEngine(this);
	
	input_mode = INK_FILE_INPUT;
	// cleanTopLevel();
	top_level = Ink_ExpressionList();
	yyin = input;
	yyparse();
	yylex_destroy();

	Ink_setCurrentEngine(backup);

	return;
}

void Ink_InterpreteEngine::startParse(string code)
{
	Ink_InterpreteEngine *backup = Ink_getCurrentEngine();
	Ink_setCurrentEngine(this);

	const char **input = (const char **)malloc(2 * sizeof(char *));

	input[0] = code.c_str();
	input[1] = NULL;
	input_mode = INK_STRING_INPUT;
	// cleanTopLevel();
	top_level = Ink_ExpressionList();
	
	Ink_setStringInput(input);
	yyparse();
	yylex_destroy();

	free(input);

	Ink_setCurrentEngine(backup);

	return;
}

Ink_Object *Ink_InterpreteEngine::execute(Ink_ContextChain *context)
{
	Ink_InterpreteEngine *backup = Ink_getCurrentEngine();
	Ink_setCurrentEngine(this);

	Ink_Object *ret;
	unsigned int i;

	if (!context) context = global_context;
	for (i = 0; i < top_level.size(); i++) {
		current_gc_engine->checkGC();
		ret = top_level[i]->eval(context);
	}

	Ink_setCurrentEngine(backup);

	return ret;
}

void Ink_InterpreteEngine::cleanExpressionList(Ink_ExpressionList exp_list)
{
	unsigned int i;

	for (i = 0; i < exp_list.size(); i++) {
		delete exp_list[i];
	}

	return;
}

void Ink_InterpreteEngine::cleanContext(Ink_ContextChain *context)
{
	Ink_ContextChain *i, *tmp;
	for (i = context->getGlobal(); i;) {
		tmp = i;
		i = i->inner;
		delete tmp;
	}

	return;
}