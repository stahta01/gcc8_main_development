#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "obstack.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "output.h"
#include "insn-attr.h"
#include "flags.h"
#include "reload.h"
#include "function.h"
#include "expr.h"
#include "optabs.h"
#include "toplev.h"
#include "recog.h"
#include "ggc.h"
#include "except.h"
#include "tm_p.h"
#include "target.h"
#include "target-def.h"
#include "debug.h"
#include "dbxout.h"
#include "langhooks.h"
#include "df.h"

static void m65x_asm_globalize_label (FILE *, const char *);

void
m65x_print_operand (FILE *stream, rtx x, int code)
{
  switch (code)
    {
    case 'h':
      gcc_assert (REG_P (x));
      asm_fprintf (stream, "%r", REGNO (x) + 1);
      break;

    default:
      switch (GET_CODE (x))
        {
	case REG:
	  asm_fprintf (stream, "%r", REGNO (x));
	  break;
	  
	case MEM:
	  output_address (XEXP (x, 0));
	  break;
	
	default:
	  output_addr_const (stream, x);
	}
    }
}

void
m65x_print_operand_address (FILE *stream, rtx x)
{
  switch (GET_CODE (x))
    {
    case PLUS:
      asm_fprintf (stream, "<foo>+<bar>");
      break;

    case REG:
      asm_fprintf (stream, "(%r)", REGNO (x));
      break;

    default:
      output_addr_const (stream, x);
    }
}

static bool
m65x_address_register_p (rtx x, int strict_p)
{
  int regno;

  if (!REG_P (x))
    return false;
  
  regno = REGNO (x);
  
  if (strict_p)
    return (regno >= FIRST_ARG_REGISTER && regno <= LAST_ZP_REGISTER);
  
  return (regno >= FIRST_ARG_REGISTER && regno <= LAST_ZP_REGISTER)
	 || regno >= FIRST_PSEUDO_REGISTER;
}

static bool
m65x_legitimate_address_p (enum machine_mode mode, rtx x, bool strict)
{
  if (CONSTANT_P (x))
    return true;
  
  if (!strict && REG_P (x))
    return true;

  if (strict)
    {
      if (m65x_address_register_p (x, strict))
        return true;

      if (GET_CODE (x) == PLUS)
        {
	  HOST_WIDE_INT modesize = GET_MODE_SIZE (mode);

	  if (m65x_address_register_p (XEXP (x, 0), strict)
	      && GET_CODE (XEXP (x, 1)) == CONST_INT
	      && INTVAL (XEXP (x, 1)) >= 0
	      && (INTVAL (XEXP (x, 1)) + modesize - 1) < 256)
	    return true;

#if 0
	  if (m65x_address_register_p (XEXP (x, 0), strict)
	      && REG_P (XEXP (x, 1))
	      && REGNO (XEXP (x, 1)) == HARD_Y_REGNUM)
	    return true;
	  
	  if (CONSTANT_P (XEXP (x, 0))
	      && REG_P (XEXP (x, 1))
	      && (REGNO (XEXP (x, 1)) == HARD_X_REGNUM
		  || REGNO (XEXP (x, 1)) == HARD_Y_REGNUM))
	    return true;
#endif
	}
    }

  return false;
}

int
m65x_mode_dependent_address_p (rtx x)
{
  /* Another big lie!  */
  return 0;
}

static rtx
m65x_function_arg (cumulative_args_t ca, enum machine_mode mode,
		   const_tree type, bool named)
{
  CUMULATIVE_ARGS *pcum = get_cumulative_args (ca);

  if (*pcum < 8)
    return gen_rtx_REG (mode, (*pcum) + FIRST_ARG_REGISTER);
  else
    return NULL_RTX;
}

static void
m65x_function_arg_advance (cumulative_args_t ca, enum machine_mode mode,
			   const_tree type, bool named)
{
  CUMULATIVE_ARGS *pcum = get_cumulative_args (ca);
  
  (*pcum) += GET_MODE_SIZE (mode);
}

static rtx
m65x_function_value (const_tree ret_type, const_tree fn_decl_or_type,
		     bool outgoing)
{
  enum machine_mode mode;
  
  mode = TYPE_MODE (ret_type);
  
  return gen_rtx_REG (mode, ACC_REGNUM);
}

static rtx
m65x_libcall_value (enum machine_mode mode, const_rtx fun)
{
  return gen_rtx_REG (mode, ACC_REGNUM);
}

static void
m65x_asm_globalize_label (FILE *stream, const char *name)
{
  fprintf (stream, "\t.export %s\n", name);
}

static void
m65x_asm_named_section (const char *name, unsigned int flags ATTRIBUTE_UNUSED,
			tree decl ATTRIBUTE_UNUSED)
{
  fprintf (asm_out_file, "\t.segment \"%s\"\n", name);
}

static section *
m65x_asm_function_section (tree decl ATTRIBUTE_UNUSED,
			   enum node_frequency freq ATTRIBUTE_UNUSED,
			   bool startup ATTRIBUTE_UNUSED,
			   bool exit ATTRIBUTE_UNUSED)
{
  /* We could actually put startup code in a different segment, but not yet.  */
  return get_named_text_section (decl, "CODE", NULL);
}

bool
m65x_hard_regno_mode_ok (int regno, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  /* For the "hard" registers, force values to have the actual LSB in the hard
     register.  */
  if (regno < 12)
    return (regno % 4) == 0;
  else
    return true;
}

static reg_class_t
m65x_spill_class (reg_class_t klass, enum machine_mode mode)
{
  switch (klass)
    {
    case HARD_ACCUM_REG:
    case HARD_X_REG:
    case HARD_Y_REG:
      return GENERAL_REGS;
    
    default:
      return NO_REGS;
    }
}

static reg_class_t
m65x_secondary_reload (bool in_p, rtx x, reg_class_t reload_class,
		       enum machine_mode reload_mode,
		       secondary_reload_info *sri)
{
  if (reload_mode == HImode)
    {
      if (in_p)
        {
	  /* X needs to be copied to a register of class RELOAD_CLASS.  */
	  if (MEM_P (x)
	      && GET_CODE (XEXP (x, 0)) == PLUS
	      && REG_P (XEXP (XEXP (x, 0), 0))
	      && REGNO_OK_FOR_BASE_P (REGNO (XEXP (XEXP (x, 0), 0)))
	      && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT)
	    {
	      if (reload_class == HARD_ACCUM_REG)
	        {
		  /* We can only do (zp),y addressing mode for the accumulator
		     reg.  */
		  sri->icode = CODE_FOR_reload_inhi_acc_indy;
		  return NO_REGS;
		}
	      else if (reload_class == ARG_REGS
		       || reload_class == CALLEE_SAVED_REGS
		       || reload_class == GENERAL_REGS)
		{
		  /* Actually it helps a little to copy things direct to ZP
		     registers too.  */
		  sri->icode = CODE_FOR_reload_inhi_reg_indy;
		  return NO_REGS;
		}
	      else
	        return HARD_ACCUM_REG;
	    }
	}
#if 0
      if (in_p)
        {
	  /* X needs to be copied to a register of class RELOAD_CLASS.  */
	  switch (reload_class)
	    {
	    case ARG_REGS:
	    case CALLEE_SAVED_REGS:
	    case GENERAL_REGS:
	      sri->icode = CODE_FOR_reload_inhi;
	      return NO_REGS;
	    
	    case HARD_ACCUM_REG:
	      fprintf (stderr, "secondary reload accumulator! (in) x=\n");
	      debug_rtx (x);
	      return HARD_REGS;
	    }
	}
      else
        {
	  /* Copy register in RELOAD_CLASS to X.  */
	  switch (reload_class)
	    {
	    case ARG_REGS:
	    case CALLEE_SAVED_REGS:
	    case GENERAL_REGS:
	      sri->icode = CODE_FOR_reload_outhi;
	      return NO_REGS;
	    
	    case HARD_ACCUM_REG:
	      fprintf (stderr, "secondary reload accumulator! (out) x=\n");
	      debug_rtx (x);
	      sri->icode = CODE_FOR_reload_outhi_acc;
	      return NO_REGS;
	    }
	}
#endif
    }

  return NO_REGS;
}

#undef TARGET_FUNCTION_ARG
#define TARGET_FUNCTION_ARG m65x_function_arg

#undef TARGET_FUNCTION_ARG_ADVANCE
#define TARGET_FUNCTION_ARG_ADVANCE m65x_function_arg_advance

#undef TARGET_FUNCTION_VALUE
#define TARGET_FUNCTION_VALUE m65x_function_value

#undef TARGET_LIBCALL_VALUE
#define TARGET_LIBCALL_VALUE m65x_libcall_value

#undef TARGET_LEGITIMATE_ADDRESS_P
#define TARGET_LEGITIMATE_ADDRESS_P m65x_legitimate_address_p

#undef TARGET_SMALL_REGISTER_CLASSES_FOR_MODE_P
#define TARGET_SMALL_REGISTER_CLASSES_FOR_MODE_P hook_bool_mode_true

#undef TARGET_SPILL_CLASS
#define TARGET_SPILL_CLASS m65x_spill_class

#undef TARGET_SECONDARY_RELOAD
#define TARGET_SECONDARY_RELOAD m65x_secondary_reload

#undef TARGET_ASM_NAMED_SECTION
#define TARGET_ASM_NAMED_SECTION m65x_asm_named_section

#undef TARGET_ASM_FUNCTION_SECTION
#define TARGET_ASM_FUNCTION_SECTION m65x_asm_function_section

#undef TARGET_ASM_GLOBALIZE_LABEL
#define TARGET_ASM_GLOBALIZE_LABEL m65x_asm_globalize_label

struct gcc_target targetm = TARGET_INITIALIZER;
