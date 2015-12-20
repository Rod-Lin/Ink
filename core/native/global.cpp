#include <stdio.h>
#include <vector>
#include "../object.h"
#include "../context.h"
#include "../expression.h"
#include "../error.h"
#include "../debug.h"
#include "../../interface/engine.h"
#include "../../interface/setting.h"
#include "native.h"

extern Ink_ExpressionList native_exp_list;
extern InterruptSignal CGC_interrupt_signal;

bool isTrue(Ink_Object *cond)
{
	if (cond->type == INK_NUMERIC) {
		if (as<Ink_Numeric>(cond)->value)
			return true;
		return false;
	}

	return cond->type != INK_NULL && cond->type != INK_UNDEFINED;
}

Ink_Object *Ink_IfExpression(Ink_ContextChain *context, unsigned int argc, Ink_Object **argv, Ink_Object *this_p)
{
	Ink_Object *cond;
	Ink_Object *ret;

	if (!argc) {
		InkWarn_If_Argument_Fault();
		return new Ink_NullObject();
	}

	ret = cond = argv[0];
	if (isTrue(cond)) {
		if (argc > 1 && argv[1]->type == INK_FUNCTION) {
			ret = argv[1]->call(context);
		}
	} else {
		if (argc > 2 && argv[2]->type == INK_FUNCTION) {
			ret = argv[2]->call(context);
		}
	}

	return ret;
}

Ink_Object *Ink_WhileExpression(Ink_ContextChain *context, unsigned int argc, Ink_Object **argv, Ink_Object *this_p)
{
	Ink_Object *cond;
	Ink_Object *block;
	Ink_Object *ret;

	if (argc < 2) {
		InkWarn_While_Argument_Require();
		return new Ink_NullObject();
	}

	cond = argv[0];
	block = argv[1];
	if (cond->type != INK_FUNCTION) {
		InkWarn_Require_Lazy_Expression();
		return new Ink_NullObject();
	} else if (block->type != INK_FUNCTION) {
		InkWarn_While_Block_Require();
		return new Ink_NullObject();
	}

	ret = new Ink_NullObject();
	while (isTrue(cond->call(context))) {
		ret = block->call(context);
		switch (CGC_interrupt_signal) {
			case INTER_RETURN:
				return ret; // fallthrough the signal
			case INTER_BREAK:
				CGC_interrupt_signal = INTER_NONE; // trap the signal
				return ret;
			case INTER_CONTINUE:
				CGC_interrupt_signal = INTER_NONE; // trap the signal
				continue;
			default: ;
		}
	}

	return ret;
}

Ink_ArrayValue cloneArrayValue(Ink_ArrayValue val)
{
	Ink_ArrayValue ret;
	unsigned int i;
	for (i = 0; i < val.size(); i++) {
		if (val[i])
			ret.push_back(new Ink_HashTable("", val[i]->value));
		else
			ret.push_back(NULL);
	}
	return ret;
}

Ink_Object *Ink_ArrayConstructor(Ink_ContextChain *context, unsigned int argc, Ink_Object **argv, Ink_Object *this_p)
{
	Ink_ContextChain *local = context->getLocal();
	Ink_Object *ret;

	if (argc) {
		if (argv[0]->type == INK_NUMERIC && argc == 1) {
			ret = new Ink_Array(Ink_ArrayValue(as<Ink_Numeric>(argv[0])->value, NULL));
		} else if (argv[0]->type == INK_ARRAY && argc == 1) {
			ret = new Ink_Array(cloneArrayValue(as<Ink_Array>(argv[0])->value));
		}else {
			Ink_ArrayValue val = Ink_ArrayValue();
			unsigned int i;
			for (i = 0; i < argc; i++) {
				val.push_back(new Ink_HashTable("", argv[i]));
			}
			ret = new Ink_Array(val);
		}
	} else {
		ret = new Ink_Array();
	}

	local->context->setSlot("this", ret);

	return ret;
}

extern int current_line_number;
extern int inkerr_current_line_number;
extern const char *yyerror_prefix;

Ink_Object *Ink_Eval(Ink_ContextChain *context, unsigned int argc, Ink_Object **argv, Ink_Object *this_p)
{
	Ink_Object *ret = NULL_OBJ;
	Ink_ExpressionList top_level_backup;
	int line_num_backup = current_line_number;
	Ink_InterpreteEngine *current_engine = Ink_getCurrentEngine();

	if (current_engine && argc && argv[0]->type == INK_STRING) {
		context->removeLast();

		top_level_backup = current_engine->top_level;

		current_line_number = inkerr_current_line_number;
		yyerror_prefix = "from eval: ";
		current_engine->startParse(as<Ink_String>(argv[0])->value);
		ret = current_engine->execute(context);

		native_exp_list.insert(native_exp_list.end(),
							   current_engine->top_level.begin(),
							   current_engine->top_level.end());
		current_engine->top_level = top_level_backup;

		context->addContext(new Ink_ContextObject());
	}
	current_line_number = line_num_backup;

	return ret;
}

bool defined(Ink_Object *obj)
{
	return obj->type != INK_UNDEFINED;
}

Ink_Object *Ink_Print(Ink_ContextChain *context, unsigned int argc, Ink_Object **argv, Ink_Object *this_p)
{
	if (argv[0]->type == INK_NUMERIC)
		printf("print(numeric): %f\n", as<Ink_Numeric>(argv[0])->value);
	else if (argv[0]->type == INK_STRING)
		printf("%s\n", as<Ink_String>(argv[0])->value.c_str());
	else if (argv[0]->type == INK_NULL)
		printf("(null)\n");
	else if (argv[0]->type == INK_UNDEFINED)
		printf("(undefined)\n");
	else
		printf("print: non-printable type: %d\n", argv[0]->type);

	return new Ink_NullObject();
}

Ink_Object *Ink_Import(Ink_ContextChain *context, unsigned int argc, Ink_Object **argv, Ink_Object *this_p)
{
	unsigned int i;
	FILE *fp;
	Ink_Object *load;
	const char *tmp;
	char *current_dir, *redirect;
	Ink_InterpreteEngine *current_engine = Ink_getCurrentEngine();
	Ink_ExpressionList top_level_backup;
	int line_num_backup = current_line_number;

	for (i = 0; i < argc; i++) {
		if (argv[i]->type == INK_STRING) {
			tmp = as<Ink_String>(argv[i])->value.c_str();
			if (!(fp = fopen(tmp, "r"))) {
				InkErr_Failed_Open_File(tmp);
				continue;
			}
			current_dir = getCurrentDir();
			redirect = getBasePath(tmp);
			if (redirect) {
				changeDir(redirect);
				free(redirect);
			}

			if (current_engine) {
				context->removeLast();
				top_level_backup = current_engine->top_level;
				current_line_number = inkerr_current_line_number;

				yyerror_prefix = "from import: ";
				current_engine->startParse(fp);
				current_engine->execute(context);

				CGC_interrupt_signal = INTER_NONE;

				native_exp_list.insert(native_exp_list.end(),
									   current_engine->top_level.begin(),
									   current_engine->top_level.end());
				current_engine->top_level = top_level_backup;

				context->addContext(new Ink_ContextObject());
			}
			fclose(fp);

			if (current_dir) {
				changeDir(current_dir);
				free(current_dir);
			}
			// run file
		} else {
			// call load method
			if ((load = getSlotWithProto(context, argv[i], "load"))->type == INK_FUNCTION) {
				load->call(context);
			} else {
				InkWarn_Not_Package();
			}
		}
	}
	current_line_number = line_num_backup;

	return NULL_OBJ;
}

Ink_Object *Ink_TypeName(Ink_ContextChain *context, unsigned int argc, Ink_Object **argv, Ink_Object *this_p)
{
	if (argc < 1) {
		InkWarn_Type_Name_Argument_Require();
		return NULL_OBJ;
	}

	return new Ink_String(getTypeName(argv[0]->type));
}

Ink_Object *Ink_NumVal(Ink_ContextChain *context, unsigned int argc, Ink_Object **argv, Ink_Object *this_p)
{
	Ink_Expression *tmp;

	if (!checkArgument(argc, argv, 1, INK_STRING) || as<Ink_String>(argv[0])->value == "") {
		return NULL_OBJ;
	}

	native_exp_list.push_back(tmp = Ink_NumericConstant::parse(as<Ink_String>(argv[0])->value));

	return tmp->eval(context);
}

Ink_Object *InkNative_Object_New(Ink_ContextChain *context, unsigned int argc, Ink_Object **argv, Ink_Object *this_p);
void Ink_GlobalMethodInit(Ink_ContextChain *context)
{
	context->context->setSlot("if", new Ink_FunctionObject(Ink_IfExpression, true));

	Ink_ParamList param_list = Ink_ParamList();
	param_list.push_back(Ink_Parameter(NULL, true));
	Ink_Object *while_func = new Ink_FunctionObject(Ink_WhileExpression, true);
	as<Ink_FunctionObject>(while_func)->param = param_list;
	context->context->setSlot("while", while_func);
	context->context->setSlot("p", new Ink_FunctionObject(Ink_Print));
	context->context->setSlot("eval", new Ink_FunctionObject(Ink_Eval));
	context->context->setSlot("import", new Ink_FunctionObject(Ink_Import));
	context->context->setSlot("typename", new Ink_FunctionObject(Ink_TypeName));
	context->context->setSlot("numval", new Ink_FunctionObject(Ink_NumVal));

	Ink_Object *array_cons = new Ink_FunctionObject(Ink_ArrayConstructor);
	context->context->setSlot("Array", array_cons);
	array_cons->setSlot("new", new Ink_FunctionObject(InkNative_Object_New));

	context->context->setSlot("undefined", new Ink_Undefined());
	context->context->setSlot("null", new Ink_NullObject());
}

int numeric_native_method_table_count = 13;
InkNative_MethodTable numeric_native_method_table[] = {
	{"+", new Ink_FunctionObject(InkNative_Numeric_Add)},
	{"-", new Ink_FunctionObject(InkNative_Numeric_Sub)},
	{"*", new Ink_FunctionObject(InkNative_Numeric_Mul)},
	{"/", new Ink_FunctionObject(InkNative_Numeric_Div)},
	{"%", new Ink_FunctionObject(InkNative_Numeric_Mod)},
	{"==", new Ink_FunctionObject(InkNative_Numeric_Equal)},
	{"!=", new Ink_FunctionObject(InkNative_Numeric_NotEqual)},
	{">", new Ink_FunctionObject(InkNative_Numeric_Greater)},
	{"<", new Ink_FunctionObject(InkNative_Numeric_Less)},
	{">=", new Ink_FunctionObject(InkNative_Numeric_GreaterOrEqual)},
	{"<=", new Ink_FunctionObject(InkNative_Numeric_LessOrEqual)},
	{"+u", new Ink_FunctionObject(InkNative_Numeric_Add_Unary)},
	{"-u", new Ink_FunctionObject(InkNative_Numeric_Sub_Unary)}
};

int string_native_method_table_count = 8;
InkNative_MethodTable string_native_method_table[] = {
	{"+", new Ink_FunctionObject(InkNative_String_Add)},
	{"<", new Ink_FunctionObject(InkNative_String_Index)},
	{">", new Ink_FunctionObject(InkNative_String_Greater)},
	{"<", new Ink_FunctionObject(InkNative_String_Less)},
	{">=", new Ink_FunctionObject(InkNative_String_GreaterOrEqual)},
	{"<=", new Ink_FunctionObject(InkNative_String_LessOrEqual)},
	{"[]", new Ink_FunctionObject(InkNative_String_Index)},
	{"length", new Ink_FunctionObject(InkNative_String_Length)}
};

int object_native_method_table_count = 11;
InkNative_MethodTable object_native_method_table[] = {
	{"->", new Ink_FunctionObject(InkNative_Object_Bond)},
	{"!!", new Ink_FunctionObject(InkNative_Object_Debond)},
	{"!", new Ink_FunctionObject(InkNative_Object_Not)},
	{"==", new Ink_FunctionObject(InkNative_Object_Equal)},
	{"!=", new Ink_FunctionObject(InkNative_Object_NotEqual)},
	{"[]", new Ink_FunctionObject(InkNative_Object_Index)},
	{"new", new Ink_FunctionObject(InkNative_Object_New)},
	{"clone", new Ink_FunctionObject(InkNative_Object_Clone)},
	{"getter", new Ink_FunctionObject(InkNative_Object_SetGetter)},
	{"setter", new Ink_FunctionObject(InkNative_Object_SetSetter)},
	{"each", new Ink_FunctionObject(InkNative_Object_Each)},
};

int array_native_method_table_count = 6;
InkNative_MethodTable array_native_method_table[] = {
	{"[]", new Ink_FunctionObject(InkNative_Array_Index)},
	{"push", new Ink_FunctionObject(InkNative_Array_Push)},
	{"size", new Ink_FunctionObject(InkNative_Array_Size)},
	{"each", new Ink_FunctionObject(InkNative_Array_Each, true)},
	{"remove", new Ink_FunctionObject(InkNative_Array_Remove)},
	{"rebuild", new Ink_FunctionObject(InkNative_Array_Rebuild)}
};

int function_native_method_table_count = 3;
InkNative_MethodTable function_native_method_table[] = {
	{"<<", new Ink_FunctionObject(InkNative_Function_Insert)},
	{"exp", new Ink_FunctionObject(InkNative_Function_GetExp)},
	{"[]", new Ink_FunctionObject(InkNative_Function_RangeCall)},
};