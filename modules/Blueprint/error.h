#ifndef _BP_ERROR_H_
#define _BP_ERROR_H_

#include "core/error.h"

inline void
InkWarn_Blueprint_While_Argument_Require(Ink_InterpreteEngine *engine)
{
	InkErro_doPrintWarning(engine, "while function requires at least 1 argument");
	return;
}

inline void
InkWarn_Blueprint_While_Require_Reference(Ink_InterpreteEngine *engine)
{
	InkErro_doPrintWarning(engine, "while function requires reference as condition expression");
	return;
}

inline void
InkWarn_Blueprint_While_Require_Block(Ink_InterpreteEngine *engine)
{
	InkErro_doPrintWarning(engine, "while function requires a block as the second argument");
	return;
}

inline void
InkWarn_Blueprint_Try_Expect_Block_After_Catch(Ink_InterpreteEngine *engine)
{
	InkErro_doPrintWarning(engine, "Expect block after catch in try function");
	return;
}

inline void
InkWarn_Blueprint_Try_Unknown_Instr(Ink_InterpreteEngine *engine, const char *instr)
{
	InkErro_doPrintWarning(engine, "Unknown instruction of try: \'$(instr)\'", instr);
	return;
}

inline void
InkWarn_Blueprint_Try_No_Argument_Follwing_Catch(Ink_InterpreteEngine *engine)
{
	InkErro_doPrintWarning(engine, "No argument following catch");
	return;
}

inline void
InkWarn_Blueprint_Try_No_Argument_Follwing_Final(Ink_InterpreteEngine *engine)
{
	InkErro_doPrintWarning(engine, "No argument following final");
	return;
}

inline void
InkWarn_Blueprint_Try_Expect_Block_After_Final(Ink_InterpreteEngine *engine)
{
	InkErro_doPrintWarning(engine, "Expect block after final");
	return;
}

inline void
InkWarn_Blueprint_Try_Unknown_Instr_Type(Ink_InterpreteEngine *engine, Ink_TypeTag type)
{
	InkErro_doPrintWarning(engine, "Unknown instruction type of try: <$(type)>",
						   getTypeName(engine, type));
	return;
}

#endif