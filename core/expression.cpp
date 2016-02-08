#include <stdio.h>
#include <string.h>
#include <math.h>
#include "error.h"
#include "object.h"
#include "context.h"
#include "expression.h"
#include "protocol.h"
#include "general.h"
#include "gc/collect.h"
#include "coroutine/coroutine.h"
#include "interface/engine.h"

#define SET_LINE_NUM (file_name_back = engine->current_file_name, \
					  line_num_back = engine->current_line_number, \
					  engine->current_file_name = (file_name ? file_name : engine->current_file_name), \
					  engine->current_line_number = (line_number >= 0 ? line_number : engine->current_line_number))
#define RESTORE_LINE_NUM (engine->current_file_name = file_name_back, engine->current_line_number = line_num_back)
#define INTER_SIGNAL_RECEIVED (engine->getSignal() != INTER_NONE)
#define IS_DEC(c) ((c) <= '9' && (c) >= '0')
#define IS_HEX(c) (IS_DEC(c) || ((c) <= 'f' && (c) >= 'a') || ((c) <= 'F' && (c) >= 'A'))
#define IS_OCT(c) ((c) <= '7' && (c) >= '0')

#define IS_CAPITAL(c) ((c) <= 'Z' && (c) >= 'A')
#define TO_LOWER(c) (IS_CAPITAL(c) ? 'a' + (c) - 'A' : (c))

#define DEC_TO_NUM(c) ((c) - '0')
#define HEX_TO_NUM(c) (IS_DEC(c) ? DEC_TO_NUM(c) : TO_LOWER(c) - 'a' + 10)
#define OCT_TO_NUM(c) (DEC_TO_NUM(c))

#define IS_LEGAL(mode, c) (IS_DEC(c) ? (c) < ('0' + (mode)) \
									 : (mode == HEX && IS_HEX(c) ? (TO_LOWER(c) - 'a') < 6 : false))

#define TO_NUM(mode, c) ((mode) <= 10 ? DEC_TO_NUM(c) : HEX_TO_NUM(c))

namespace ink {

using namespace std;

Ink_NumericValue Ink_NumericConstant::parseNumeric(string code, bool *is_success)
{
	Ink_NumericValue ret = 0.0;
	string::size_type i;
	int flag = 1, decimal = 0;
	enum {
		DEC = 10,
		HEX = 16,
		OCT = 8
	} mode = DEC;

	if (is_success)
		*is_success = true;

	if (code.length() && code[0] == '-') {
		flag = -1;
		code = code.substr(1);
	}

	if (code.length() && code[0] == '0') {
		code = code.substr(1);
		if (code.length()) {
			if (code[0] == 'x' || code[0] == 'X') {
				mode = HEX;
				code = code.substr(1);
			} else if (code[0] == 'o' || code[0] == 'O') {
				mode = OCT;
				code = code.substr(1);
			}
		}
	}

	for (i = 0; i < code.length(); i++) {
		if (code[i] == '.') {
			decimal++;
			continue;
		}
		if (IS_LEGAL(mode, code[i])) {
			if (decimal > 0) {
				ret += TO_NUM(mode, code[i]) / pow((int)mode, decimal);
				decimal++;
			} else {
				ret = ret * mode + TO_NUM(mode, code[i]);
			}
		} else {
			fprintf(stderr, "Illegal character \'%c\'\n", code[i]);
			if (is_success)
				*is_success = false;
			break;
		}
	}

	return ret * flag;
}

Ink_Expression *Ink_NumericConstant::parse(string code)
{
	bool is_success = false;
	Ink_NumericValue val = Ink_NumericConstant::parseNumeric(code, &is_success);

	if (is_success)
		return new Ink_NumericConstant(val);
	else
	
	return NULL;
}

Ink_Object *Ink_CommaExpression::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	const char *file_name_back;
	Ink_LineNoType line_num_back;
	SET_LINE_NUM;

	Ink_Object *ret = NULL_OBJ;
	Ink_ExpressionList::size_type i;

	for (i = 0; i < exp_list.size(); i++) {
		ret = exp_list[i]->eval(engine, context_chain);
		if (INTER_SIGNAL_RECEIVED) {
			RESTORE_LINE_NUM;
			return engine->getInterruptValue();
		}
	}

	RESTORE_LINE_NUM;
	return ret;
}

Ink_Object *Ink_YieldExpression::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	const char *file_name_back;
	Ink_LineNoType line_num_back;
	SET_LINE_NUM;

	InkCoro_Scheduler *sched;

	if (!(sched = engine->currentScheduler())) {
		InkError_Yield_Without_Coroutine(engine);
		return NULL_OBJ;
	}

	Ink_Object *ret = ret_val ? ret_val->eval(engine, context_chain) : NULL_OBJ;
	if (INTER_SIGNAL_RECEIVED) {
		RESTORE_LINE_NUM;
		return engine->getInterruptValue();
	}
	engine->setInterruptValue(ret);

	IGC_CollectEngine *gc_engine_backup = engine->getCurrentGC();

	sched->yield();

	engine->setCurrentGC(gc_engine_backup);

	RESTORE_LINE_NUM;
	return engine->getInterruptValue();
}

Ink_Object *Ink_InterruptExpression::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	const char *file_name_back;
	Ink_LineNoType line_num_back;
	Ink_InterruptSignal tmp_sig;
	SET_LINE_NUM;

	Ink_Object *ret = ret_val ? ret_val->eval(engine, context_chain) : NULL_OBJ;
	if (INTER_SIGNAL_RECEIVED) {
		RESTORE_LINE_NUM;
		return engine->getInterruptValue();
	}

	RESTORE_LINE_NUM;

	if (custom_sig) {
		tmp_sig = engine->getCustomInterruptSignal(*custom_sig);
		if (!tmp_sig) {
			InkWarn_Undefined_Custom_Interrupt_Name(engine, custom_sig->c_str());
			return ret;
		}
		engine->setInterrupt(tmp_sig, ret);
		return ret;
	}

	engine->setInterrupt(sig, ret);

	return ret;
}

Ink_Object *Ink_LogicExpression::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	const char *file_name_back;
	Ink_LineNoType line_num_back;
	bool ret_val = false;
	SET_LINE_NUM;

	/* first eval left hand side */
	Ink_Object *lhs = lval->eval(engine, context_chain);
	Ink_Object *rhs;

	/* interrupt signal received */
	if (INTER_SIGNAL_RECEIVED) {
		RESTORE_LINE_NUM;
		return engine->getInterruptValue();
	}

	if (isTrue(lhs)) {
		/* left hand side true */
		if (type == LOGIC_OR)
			/* if operator is or, return true directly */
			ret_val = true;
		else {
			/* if operator is and, make sure the right hand side is true, too */
			rhs = rval->eval(engine, context_chain);
			if (INTER_SIGNAL_RECEIVED) {
				RESTORE_LINE_NUM;
				return engine->getInterruptValue();
			}
			if (isTrue(rhs)) {
				ret_val = true;
			}
		}
	} else {
		/* if operator is or, judge right hand side */
		if (type == LOGIC_OR && isTrue(rval->eval(engine, context_chain))) {
			if (INTER_SIGNAL_RECEIVED) {
				RESTORE_LINE_NUM;
				return engine->getInterruptValue();
			}
			ret_val = true;
		}
	}

	RESTORE_LINE_NUM;

	return new Ink_Numeric(engine, ret_val);
}

Ink_Object *Ink_AssignmentExpression::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	const char *file_name_back;
	Ink_LineNoType line_num_back;
	SET_LINE_NUM;

	Ink_Object *rval_ret;
	Ink_Object *lval_ret;
	Ink_Object **tmp;
	Ink_Object *ret;

	rval_ret = rval->eval(engine, context_chain);
	/* eval right hand side first */
	if (INTER_SIGNAL_RECEIVED) {
		RESTORE_LINE_NUM;
		return engine->getInterruptValue();
	}

	lval_ret = lval->eval(engine, context_chain, Ink_EvalFlag(true));
	/* left hand side next */
	if (INTER_SIGNAL_RECEIVED) {
		RESTORE_LINE_NUM;
		return engine->getInterruptValue();
	}

	if (lval_ret->address) {
		if (lval_ret->address->setter) { /* if has setter, call it */
			tmp = (Ink_Object **)malloc(sizeof(Ink_Object *));
			tmp[0] = rval_ret;
			lval_ret->address->setter->setSlot("base", lval_ret);
			ret = lval_ret->address->setter->call(engine, context_chain, 1, tmp);
			// engine->setSignal(INTER_NONE);
			free(tmp);
			if (INTER_SIGNAL_RECEIVED) {
				RESTORE_LINE_NUM;
				return engine->getInterruptValue();
			}
			return ret;
		} else {
			/* no setter, directly assign */
			lval_ret->address->setValue(rval_ret);
		}
		return is_return_lval ? lval_ret : rval_ret;
	}

	InkWarn_Assigning_Unassignable_Expression(engine);
	//abort();

	RESTORE_LINE_NUM;
	return NULL_OBJ;
}

Ink_Object *Ink_HashTableExpression::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	const char *file_name_back;
	Ink_LineNoType line_num_back;
	SET_LINE_NUM;

	Ink_Object *ret = new Ink_Object(engine), *key;
	Ink_HashTableMapping::size_type i;

	for (i = 0; i < mapping.size(); i++) {
		/* two possibility: 1. identifier key; 2. expression key with brackets */
		if (mapping[i]->name) {
			ret->setSlot(mapping[i]->name->c_str(), mapping[i]->value->eval(engine, context_chain));
			if (INTER_SIGNAL_RECEIVED) {
				RESTORE_LINE_NUM;
				return engine->getInterruptValue();
			}
		} else {
			key = mapping[i]->key->eval(engine, context_chain);
			if (INTER_SIGNAL_RECEIVED) {
				RESTORE_LINE_NUM;
				return engine->getInterruptValue();
			}
			if (key->type != INK_STRING) {
				InkWarn_Hash_Table_Mapping_Expect_String(engine);
				return NULL_OBJ;
			}
			string *tmp = new string(as<Ink_String>(key)->getValue());
			ret->setSlot(tmp->c_str(),
						 mapping[i]->value->eval(engine, context_chain), true, tmp);
			if (INTER_SIGNAL_RECEIVED) {
				RESTORE_LINE_NUM;
				return engine->getInterruptValue();
			}
		}
	}

	RESTORE_LINE_NUM;
	return ret;
}

Ink_Object *Ink_TableExpression::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	const char *file_name_back;
	Ink_LineNoType line_num_back;
	SET_LINE_NUM;

	Ink_ArrayValue val = Ink_ArrayValue();
	Ink_ArrayValue::size_type i;

	for (i = 0; i < elem_list.size(); i++) {
		val.push_back(new Ink_HashTable("", elem_list[i]->eval(engine, context_chain)));
		if (INTER_SIGNAL_RECEIVED) {
			RESTORE_LINE_NUM;
			Ink_Array::disposeArrayValue(val);
			return engine->getInterruptValue();
		}
	}

	RESTORE_LINE_NUM;
	return new Ink_Array(engine, val);
}

Ink_Object *Ink_HashExpression::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	const char *file_name_back;
	Ink_LineNoType line_num_back;
	SET_LINE_NUM;

	Ink_Object *base_obj = base->eval(engine, context_chain);
	if (INTER_SIGNAL_RECEIVED) {
		RESTORE_LINE_NUM;
		return engine->getInterruptValue();
	}

	RESTORE_LINE_NUM;
	return getSlot(engine, context_chain, base_obj, slot_id->c_str(), flags);
}

Ink_HashExpression::ProtoSearchRet Ink_HashExpression::searchPrototype(Ink_InterpreteEngine *engine, Ink_Object *obj, const char *id)
{
	if (engine->prototypeHasTraced(obj)) {
		InkWarn_Circular_Prototype_Reference(engine);
		return Ink_HashExpression::ProtoSearchRet(NULL, obj);
	}
	engine->addPrototypeTrace(obj);

	Ink_HashTable *hash = obj->getSlotMapping(engine, id);
	Ink_HashTable *proto;
	Ink_Object *proto_obj = NULL;
	Ink_HashExpression::ProtoSearchRet search_res;

	if (!hash) { /* cannot find slot in object itself */
		/* get prototype */
		proto = obj->getSlotMapping(engine, "prototype");

		/* prototype exists and it's not undefined */
		if (proto && proto->getValue()->type != INK_UNDEFINED) {
			/* search the slot in prototype, and get the result */
			hash = (search_res = searchPrototype(engine, proto->getValue(), id)).hash;
			proto_obj = search_res.base;
		}
	}

	/* return result with base pointed to the prototype(if has)
	 * in which found the slot
	 */
	return Ink_HashExpression::ProtoSearchRet(hash, proto_obj ? proto_obj : obj);
}

Ink_Object *Ink_HashExpression::getSlot(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain,
										Ink_Object *obj, const char *id, Ink_EvalFlag flags, string *id_p)
{
	const char *debug_name_back;
	Ink_HashTable *hash, *address;
	Ink_Object *base = obj, *ret = NULL, *tmp;
	Ink_Object **argv;
	ProtoSearchRet search_res;
	bool is_from_proto = false;

	if (!(hash = obj->getSlotMapping(engine, id, &is_from_proto)) /* cannot find slot in the origin object */) {
		if (obj->type == INK_UNDEFINED) {
			InkWarn_Get_Slot_Of_Undefined(engine, id);
		}
		if (id_p) {
			address = obj->setSlot(id, NULL, id_p);
		} else {
			id_p = new string(id);
			address = obj->setSlot(id_p->c_str(), NULL, id_p);
		}
		if ((tmp = obj->getSlot(engine, "missing"))->type == INK_FUNCTION) {
			/* has missing method, call it */
			tmp->setSlot("base", obj);
			argv = (Ink_Object **)malloc(sizeof(Ink_Object *));
			argv[0] = new Ink_String(engine, string(id));
			ret = tmp->call(engine, context_chain, 1, argv);
			free(argv);
			goto END;
		} else {
			/* return undefined */
			ret = UNDEFINED;
		}
#if 0
		/* search prototype */
		engine->initPrototypeSearch();
		hash = (search_res = searchPrototype(engine, obj, id)).hash;

		/* create barrier to prevent changes on prototype */
		address = obj->setSlot(id, NULL, id_p);
		if (hash) { /* if found the slot in prototype */
			// base = search_res.base; /* set base as the prototype(to make some native method run correctly) */
			ret = hash->getValue();
		} else {
			if ((tmp = obj->getSlot(engine, "missing"))->type == INK_FUNCTION) {
				/* has missing method, call it */
				tmp->setSlot("base", obj);
				argv = (Ink_Object **)malloc(sizeof(Ink_Object *));
				argv[0] = new Ink_String(engine, string(id));
				ret = tmp->call(engine, context_chain, 1, argv);
				free(argv);
				goto END;
			} else {
				/* return undefined */
				ret = UNDEFINED;
			}
		}
#endif
	} else {
		/* found slot correctly */
		if (is_from_proto) {
			ret = hash->getValue()->clone(engine);
			if (id_p)
				address = obj->setSlot(id, NULL, id_p);
			else {
				id_p = new string(id);
				address = obj->setSlot(id_p->c_str(), NULL, id_p);
			}
		} else {
			ret = hash->getValue();
			address = hash;
			delete id_p;
		}
	}

	/* set address for possible assignment */
	ret->address = address;
	/* set base */
	debug_name_back = base->getDebugName();
	ret->setSlot("base", base);
	base->setDebugName(debug_name_back);

	/* call getter if has one */
	if (!flags.is_left_value && hash && hash->getter) {
		hash->getter->setSlot("base", hash->getValue());
		ret = hash->getter->call(engine, context_chain, 0, NULL);
		// /* trap all interrupt signal */
		// engine->setSignal(INTER_NONE);
		if (INTER_SIGNAL_RECEIVED) {
			return engine->getInterruptValue();
		}
	}

END:
	return ret;
}

Ink_Object *Ink_FunctionExpression::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	const char *file_name_back;
	Ink_LineNoType line_num_back;
	SET_LINE_NUM;
	Ink_Protocol protocol;

	if (protocol_name && (protocol = engine->findProtocol(protocol_name->c_str()))) {
		RESTORE_LINE_NUM;
		return protocol(engine, param, exp_list, is_macro ? NULL : context_chain->copyContextChain());
	}

	RESTORE_LINE_NUM;
	return new Ink_FunctionObject(engine, param, exp_list, is_macro ? NULL : context_chain->copyContextChain(),
								  is_inline || is_macro);
}

/* expand argument -- expand array object to parameter */
inline Ink_ArgumentList expandArgument(Ink_InterpreteEngine *engine, Ink_Object *obj)
{
	Ink_ArgumentList ret = Ink_ArgumentList();
	Ink_ArrayValue arr_val;
	Ink_ArrayValue::size_type i;

	if (!obj || obj->type != INK_ARRAY) {
		InkWarn_With_Attachment_Require(engine);
		return ret;
	}

	arr_val = as<Ink_Array>(obj)->value;

	for (i = 0; i < arr_val.size(); i++) {
		ret.push_back(new Ink_Argument(new Ink_ShellExpression(arr_val[i]->getValue())));
	}
	return ret;
}

Ink_Object *Ink_CallExpression::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	const char *file_name_back;
	Ink_LineNoType line_num_back;
	SET_LINE_NUM;

	Ink_ArgumentList::size_type i;
	Ink_Object **argv = NULL;
	Ink_Object *ret_val, *expandee;
	/* eval callee to get parameter declaration */
	Ink_Object *func = callee->eval(engine, context_chain);
	if (INTER_SIGNAL_RECEIVED) {
		RESTORE_LINE_NUM;
		return engine->getInterruptValue();
	}
	Ink_ParamList param_list = Ink_ParamList();
	Ink_ArgumentList dispose_list, tmp_arg_list, another_tmp_arg_list;
	Ink_ArgcType argc;

	if (func->type == INK_FUNCTION) {
		param_list = as<Ink_FunctionObject>(func)->param;
	}
	if (is_new) {
		func = Ink_HashExpression::getSlot(engine, context_chain, func, "new");
	}


	tmp_arg_list = Ink_ArgumentList();
	for (Ink_ArgumentList::iterator arg_list_iter = arg_list.begin();
		 arg_list_iter != arg_list.end(); arg_list_iter++) {
		if ((*arg_list_iter)->is_expand) {
			/* if the argument is 'with' argument attachment */

			/* eval expandee */
			expandee = (*arg_list_iter)->expandee->eval(engine, context_chain);
			if (INTER_SIGNAL_RECEIVED) {
				ret_val = engine->getInterruptValue();
				goto DISPOSE_LIST;
			}

			/* expand argument */
			another_tmp_arg_list = expandArgument(engine, expandee);

			/* insert expanded argument to dispose list and temporary argument list */
			dispose_list.insert(dispose_list.end(), another_tmp_arg_list.begin(), another_tmp_arg_list.end());
			tmp_arg_list.insert(tmp_arg_list.end(), another_tmp_arg_list.begin(), another_tmp_arg_list.end());
		} else {
			/* normal argument, directly push to argument list */
			tmp_arg_list.push_back(*arg_list_iter);
		}
	}

	if (tmp_arg_list.size()) {
		/* allocate argument list */
		argv = (Ink_Object **)malloc(tmp_arg_list.size() * sizeof(Ink_Object *));

		/* set argument list, according to the parameter declaration */
		for (i = 0; i < tmp_arg_list.size(); i++) {
			if (i < param_list.size() && param_list[i].is_ref) {
				/* if the parameter is declared as reference, seal the expression to a anonymous function */
				if (param_list[i].is_variant) {
					for (; i < tmp_arg_list.size(); i++) {
						if (tmp_arg_list[i]->arg->is_unknown) {
							argv[i] = new Ink_Unknown(engine);
						} else {
							Ink_ExpressionList exp_list = Ink_ExpressionList();
							exp_list.push_back(tmp_arg_list[i]->arg);
							argv[i] = new Ink_FunctionObject(engine, Ink_ParamList(), exp_list,
															 context_chain->copyContextChain(),
															 true);
						}
					}
				} else {
					if (tmp_arg_list[i]->arg->is_unknown) {
						argv[i] = new Ink_Unknown(engine);
					} else {
						Ink_ExpressionList exp_list = Ink_ExpressionList();
						exp_list.push_back(tmp_arg_list[i]->arg);
						argv[i] = new Ink_FunctionObject(engine, Ink_ParamList(), exp_list,
														 context_chain->copyContextChain(),
														 true);
					}
				}
			} else {
				/* normal argument */
				argv[i] = tmp_arg_list[i]->arg->eval(engine, context_chain);
				if (INTER_SIGNAL_RECEIVED) {
					ret_val = engine->getInterruptValue();
					/* goto dispose and interrupt */
					goto DISPOSE_ARGV;
				}
			}
		}
	}

	argc = tmp_arg_list.size();

	ret_val = func->call(engine, context_chain, argc, argv);

DISPOSE_ARGV:
	free(argv);

DISPOSE_LIST:
	for (i = 0; i < dispose_list.size(); i++) {
		delete dispose_list[i];
	}

	RESTORE_LINE_NUM;

	return ret_val;
}

Ink_Object *Ink_IdentifierExpression::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	const char *file_name_back;
	Ink_LineNoType line_num_back;
	SET_LINE_NUM;

	Ink_Object *ret;
	ret = getContextSlot(engine, context_chain, id->c_str(), context_type, flags, if_create_slot);

	RESTORE_LINE_NUM;
	return ret;
}

Ink_Object *Ink_IdentifierExpression::getContextSlot(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, const char *name,
													 Ink_IDContextType context_type, Ink_EvalFlag flags, bool if_create_slot, string *name_p)
{
	/* Variables */
	Ink_HashTable *hash, *missing;
	Ink_ContextChain *local = context_chain->getLocal();
	Ink_ContextChain *global = context_chain->getGlobal();
	Ink_ContextChain *dest_context = local, *base_context = NULL;
	Ink_Object *tmp, *ret;
	Ink_Object **argv;

	/* Determine the type of reference:
	 * 1. local
	 *		find slot in the local context
	 * 2. global
	 *		find slot in the global context
	 * 3. default
	 *		search all contexts to find slot
	 */
	switch (context_type) {
		case ID_LOCAL:
			hash = local->context->getSlotMapping(engine, name);
			missing = local->context->getSlotMapping(engine, "missing");
			base_context = local;
			break;
		case ID_GLOBAL:
			hash = global->context->getSlotMapping(engine, name);
			missing = global->context->getSlotMapping(engine, "missing");
			dest_context = global;
			base_context = global;
			break;
		default:
			hash = context_chain->searchSlotMapping(engine, name);
			missing = context_chain->searchSlotMapping(engine, "missing", &base_context);
			break;
	}

	/* if the slot cannot be found */
	if (!hash) {
		if (if_create_slot) { /* if has the "var" keyword */
			ret = new Ink_Object(engine);
			hash = dest_context->context->setSlot(name, ret, name_p);
		} else { /* generate a undefined value */
			if (missing && missing->getValue()->type == INK_FUNCTION) {
				argv = (Ink_Object **)malloc(sizeof(Ink_Object *));
				argv[0] = new Ink_String(engine, name);
				ret = missing->getValue()->call(engine, context_chain, 1, argv);
				free(argv);
				goto END;
			} else {
				ret = UNDEFINED;
			}
			hash = dest_context->context->setSlot(name, NULL, name_p);
		}
	} else {
		ret = hash->getValue(); /* get value */
		if (name_p)
			delete name_p;
	}
	ret->address = hash; /* set its address for assigning */
	if (base_context)
		ret->setSlot("base", base_context->context);

	/* if it's not a left value reference(which will call setter in assign exp) and has getter, call it */
	if (!flags.is_left_value && hash->getter) {
		if (base_context)
			hash->getter->setSlot("base", base_context->context);
		tmp = hash->getter->call(engine, context_chain, 0, NULL);

		// /* trap all interrupt signal */
		// engine->setSignal(INTER_NONE);
		if (INTER_SIGNAL_RECEIVED) {
			return engine->getInterruptValue();
		}
		return tmp;
	}
	// hash->value->setSlot("this", hash->value);

END:
	return ret;
}

Ink_Object *Ink_ArrayLiteral::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	const char *file_name_back;
	Ink_LineNoType line_num_back;
	SET_LINE_NUM;
	Ink_ArrayValue val = Ink_ArrayValue();
	Ink_ArrayValue::size_type i;

	for (i = 0; i < elem_list.size(); i++) {
		val.push_back(new Ink_HashTable("", elem_list[i]->eval(engine, context_chain)));
		if (INTER_SIGNAL_RECEIVED) {
			RESTORE_LINE_NUM;
			Ink_Array::disposeArrayValue(val);
			return engine->getInterruptValue();
		}
	}

	RESTORE_LINE_NUM;
	return new Ink_Array(engine, val);
}

Ink_Object *Ink_NullConstant::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	return NULL_OBJ;
}

Ink_Object *Ink_UndefinedConstant::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	return UNDEFINED;
}

Ink_Object *Ink_NumericConstant::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	return new Ink_Numeric(engine, value);
}

Ink_Object *Ink_StringConstant::eval(Ink_InterpreteEngine *engine, Ink_ContextChain *context_chain, Ink_EvalFlag flags)
{
	return new Ink_String(engine, *value);
}

}
