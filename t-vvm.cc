/*
 * Copyright (c) 1998 Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */
#if !defined(WINNT)
#ident "$Id: t-vvm.cc,v 1.3 1998/11/07 19:17:10 steve Exp $"
#endif

# include  <iostream>
# include  <strstream>
# include  <string>
# include  <typeinfo>
# include  "netlist.h"
# include  "target.h"

static string make_temp()
{
      static unsigned counter = 0;
      ostrstream str;
      str << "TMP" << counter << ends;
      counter += 1;
      return str.str();
}

class target_vvm : public target_t {
    public:
      virtual void start_design(ostream&os, const Design*);
      virtual void signal(ostream&os, const NetNet*);
      virtual void logic(ostream&os, const NetLogic*);
      virtual void bufz(ostream&os, const NetBUFZ*);
      virtual void net_pevent(ostream&os, const NetPEvent*);
      virtual void start_process(ostream&os, const NetProcTop*);
      virtual void proc_assign(ostream&os, const NetAssign*);
      virtual void proc_block(ostream&os, const NetBlock*);
      virtual void proc_condit(ostream&os, const NetCondit*);
      virtual void proc_task(ostream&os, const NetTask*);
      virtual void proc_event(ostream&os, const NetPEvent*);
      virtual void proc_delay(ostream&os, const NetPDelay*);
      virtual void end_process(ostream&os, const NetProcTop*);
      virtual void end_design(ostream&os, const Design*);

    private:
      void emit_gate_outputfun_(const NetNode*);

      ostrstream delayed;
      unsigned process_counter;
      unsigned thread_step_;
};

/*
 * This class emits code for the rvalue of a procedural
 * assignment. The expression is evaluated to fit the width
 * specified.
 */
class vvm_proc_rval  : public expr_scan_t {

    public:
      explicit vvm_proc_rval(ostream&os)
      : result(""), os_(os) { }

      string result;

    private:
      ostream&os_;

    private:
      virtual void expr_const(const NetEConst*);
      virtual void expr_ident(const NetEIdent*);
      virtual void expr_signal(const NetESignal*);
      virtual void expr_unary(const NetEUnary*);
      virtual void expr_binary(const NetEBinary*);
};

void vvm_proc_rval::expr_const(const NetEConst*expr)
{
      string tname = make_temp();
      os_ << "        vvm_bitset_t<" << expr->expr_width() << "> "
	  << tname << ";" << endl;
      for (unsigned idx = 0 ;  idx < expr->expr_width() ;  idx += 1) {
	    os_ << "        " << tname << "[" << idx << "] = ";
	    switch (expr->value().get(idx)) {
		case verinum::V0:
		  os_ << "V0";
		  break;
		case verinum::V1:
		  os_ << "V1";
		  break;
		case verinum::Vx:
		  os_ << "Vx";
		  break;
		case verinum::Vz:
		  os_ << "Vz";
		  break;
	    }
	    os_ << ";" << endl;
      }

      result = tname;
}

void vvm_proc_rval::expr_ident(const NetEIdent*expr)
{
      result = mangle(expr->name());
}

void vvm_proc_rval::expr_signal(const NetESignal*expr)
{
      result = mangle(expr->name());
}

void vvm_proc_rval::expr_unary(const NetEUnary*expr)
{
      expr->expr()->expr_scan(this);
      string tname = make_temp();

      os_ << "        vvm_bitset_t<" << expr->expr_width() << "> "
	  << tname << " = ";
      switch (expr->op()) {
	  case '~':
	    os_ << "vvm_unop_not(" << result << ");"
		<< endl;
	    break;
	  default:
	    cerr << "vvm: Unhandled unary op `" << expr->op() << "'"
		 << endl;
	    os_ << result << ";" << endl;
	    break;
      }

      result = tname;
}

void vvm_proc_rval::expr_binary(const NetEBinary*expr)
{
      expr->left()->expr_scan(this);
      string lres = result;

      expr->right()->expr_scan(this);
      string rres = result;

      result = make_temp();
      os_ << "        vvm_bitset_t<" << expr->expr_width() << ">" <<
	    result << ";" << endl;
      switch (expr->op()) {
	  case 'e':
	    os_ << "        " << result << " = vvm_binop_eq(" << lres
		<< "," << rres << ");" << endl;
	    break;
	  case '+':
	    os_ << "        " << result << " = vvm_binop_plus(" << lres
		<< "," << rres << ");" << endl;
	    break;
	  default:
	    cerr << "vvm: Unhandled binary op `" << expr->op() << "': "
		 << *expr << endl;
	    os_ << lres << ";" << endl;
	    result = lres;
	    break;
      }
}

static string emit_proc_rval(ostream&os, const NetExpr*expr)
{
      vvm_proc_rval scan (os);
      expr->expr_scan(&scan);
      return scan.result;
}

class vvm_parm_rval  : public expr_scan_t {

    public:
      explicit vvm_parm_rval(ostream&os)
      : result(""), os_(os) { }

      string result;

    private:
      virtual void expr_const(const NetEConst*);
      virtual void expr_ident(const NetEIdent*);
      virtual void expr_signal(const NetESignal*);

    private:
      ostream&os_;
};

void vvm_parm_rval::expr_const(const NetEConst*expr)
{
      if (expr->value().is_string()) {
	    result = "\"";
	    result = result + expr->value().as_string() + "\"";
	    return;
      }
}

void vvm_parm_rval::expr_ident(const NetEIdent*expr)
{
      if (expr->name() == "$time") {
	    string res = make_temp();
	    os_ << "        vvm_calltf_parm " << res <<
		  "(vvm_calltf_parm::TIME);" << endl;
	    result = res;
      } else {
	    cerr << "Unhandled identifier: " << expr->name() << endl;
      }
}

void vvm_parm_rval::expr_signal(const NetESignal*expr)
{
      string res = make_temp();
      os_ << "        vvm_calltf_parm::SIG " << res << ";" << endl;
      os_ << "        " << res << ".bits = &" <<
	    mangle(expr->name()) << ";" << endl;
      os_ << "        " << res << ".mon = &" <<
	    mangle(expr->name()) << "_mon;" << endl;
      result = res;
}

static string emit_parm_rval(ostream&os, const NetExpr*expr)
{
      vvm_parm_rval scan (os);
      expr->expr_scan(&scan);
      return scan.result;
}

void target_vvm::start_design(ostream&os, const Design*mod)
{
      os << "# include \"vvm.h\"" << endl;
      os << "# include \"vvm_gates.h\"" << endl;
      os << "# include \"vvm_func.h\"" << endl;
      os << "# include \"vvm_calltf.h\"" << endl;
      os << "# include \"vvm_thread.h\"" << endl;
      process_counter = 0;
}

void target_vvm::end_design(ostream&os, const Design*mod)
{
      delayed << ends;
      os << delayed.str();

      os << "main()" << endl << "{" << endl;
      os << "      vvm_simulation sim;" << endl;

      for (unsigned idx = 0 ;  idx < process_counter ;  idx += 1)
	    os << "      thread" << (idx+1) << "_t thread_" <<
		  (idx+1) << "(&sim);" << endl;

      os << "      sim.run();" << endl;
      os << "}" << endl;
}

void target_vvm::signal(ostream&os, const NetNet*sig)
{
      os << "static vvm_bitset_t<" << sig->pin_count() << "> " <<
	    mangle(sig->name()) << "; /* " << sig->name() << " */" << endl;
      os << "static vvm_monitor_t " << mangle(sig->name()) << "_mon(\""
	 << sig->name() << "\");" << endl;
}

/*
 * This method handles writing output functions for gates that have a
 * single output (at pin 0). This writes the output_fun method into
 * the delayed stream to be emitted to the output file later.
 */
void target_vvm::emit_gate_outputfun_(const NetNode*gate)
{
      delayed << "static void " << mangle(gate->name()) <<
	    "_output_fun(vvm_simulation*sim, vvm_bit_t val)" <<
	    endl << "{" << endl;

	/* The output function connects to pin 0 of the netlist part
	   and causes the inputs that it is connected to to be set
	   with the new value. */

      const NetObj*cur;
      unsigned pin;
      gate->pin(0).next_link(cur, pin);
      while (cur != gate) {
	      // Skip signals
	    if (const NetNet*sig = dynamic_cast<const NetNet*>(cur)) {

		  delayed << "      " << mangle(sig->name()) << "[" <<
			pin << "] = val;" << endl;
		  delayed << "      " << mangle(sig->name()) <<
			"_mon.trigger(sim);" << endl;

	    } else {
		  delayed << "      " << mangle(cur->name()) << ".set(sim, "
			  << pin << ", val);" << endl;
	    }

	    cur->pin(pin).next_link(cur, pin);
      }

      delayed << "}" << endl;
}

void target_vvm::logic(ostream&os, const NetLogic*gate)
{
      os << "static void " << mangle(gate->name()) <<
	    "_output_fun(vvm_simulation*, vvm_bit_t);" << endl;

      switch (gate->type()) {
	  case NetLogic::AND:
	    os << "static vvm_and" << "<" << gate->pin_count()-1 <<
		  "," << gate->delay1() << "> ";
	    break;
	  case NetLogic::NAND:
	    os << "static vvm_nand" << "<" << gate->pin_count()-1 <<
		  "," << gate->delay1() << "> ";
	    break;
	  case NetLogic::NOR:
	    os << "static vvm_nor" << "<" << gate->pin_count()-1 <<
		  "," << gate->delay1() << "> ";
	    break;
	  case NetLogic::NOT:
	    os << "static vvm_not" << "<" << gate->delay1() << "> ";
	    break;
	  case NetLogic::OR:
	    os << "static vvm_or" << "<" << gate->pin_count()-1 <<
		  "," << gate->delay1() << "> ";
	    break;
	  case NetLogic::XOR:
	    os << "static vvm_xor" << "<" << gate->pin_count()-1 <<
		  "," << gate->delay1() << "> ";
	    break;
      }

      os << mangle(gate->name()) << "(&" <<
	    mangle(gate->name()) << "_output_fun);" << endl;

      emit_gate_outputfun_(gate);
}

void target_vvm::bufz(ostream&os, const NetBUFZ*gate)
{
      os << "static void " << mangle(gate->name()) <<
	    "_output_fun(vvm_simulation*, vvm_bit_t);" << endl;

      os << "static vvm_bufz " << mangle(gate->name()) << "(&" <<
	    mangle(gate->name()) << "_output_fun);" << endl;

      emit_gate_outputfun_(gate);
}

/*
 * The net_pevent device is a synthetic device type--a fabrication of
 * the elaboration phase. An event device receives value changes from
 * the attached signal. It is an input only device, its only value
 * being the side-effects that threads waiting on events can be
 * awakened.
 *
 * The proc_event method handles the other half of this, the process
 * that blocks on the event.
 */
void target_vvm::net_pevent(ostream&os, const NetPEvent*gate)
{
      os << "static vvm_pevent " << mangle(gate->name()) << ";"
	    " /* " << gate->name() << " */" << endl;
}

void target_vvm::start_process(ostream&os, const NetProcTop*proc)
{
      process_counter += 1;
      thread_step_ = 0;

      os << "class thread" << process_counter <<
	    "_t : public vvm_thread {" << endl;

      os << "    public:" << endl;
      os << "      thread" << process_counter <<
	    "_t(vvm_simulation*sim)" << endl;
      os << "      : vvm_thread(sim), step_(&step_0_)" << endl;
      os << "      { }" << endl;
      os << "      ~thread" << process_counter << "_t() { }" << endl;
      os << endl;
      os << "      void go() { (this->*step_)(); }" << endl;
      os << "    private:" << endl;
      os << "      void (thread" << process_counter <<
	    "_t::*step_)();" << endl;

      os << "      void step_0_() {" << endl;
}

/*
 * This method generates code for a procedural assignment. The lval is
 * a signal, but the assignment should generate code to go to all the
 * connected devices/events.
 */
void target_vvm::proc_assign(ostream&os, const NetAssign*net)
{
      string rval = emit_proc_rval(os, net->rval());

      os << "        // " << net->lval()->name() << " = ";
      net->rval()->dump(os);
      os << endl;

      os << "        " << mangle(net->lval()->name()) << " = " << rval <<
	    ";" << endl;

      os << "        " << mangle(net->lval()->name()) <<
	    "_mon.trigger(sim_);" << endl;


	/* Not only is the lvalue signal assigned to, send the bits to
	   all the other pins that are connected to this signal. */

      for (unsigned idx = 0 ;  idx < net->pin_count() ;  idx += 1) {
	    const NetObj*cur;
	    unsigned pin;
	    for (net->lval()->pin(idx).next_link(cur, pin)
		       ; cur != net->lval()
		       ; cur->pin(pin).next_link(cur, pin)) {

		    // Skip NetAssign nodes. They are output-only.
		  if (dynamic_cast<const NetAssign*>(cur))
			continue;

		  if (const NetNet*sig = dynamic_cast<const NetNet*>(cur)) {
			os << "        " << mangle(sig->name()) << "[" <<
			      pin << "] = " << rval << "[" << idx <<
			      "];" << endl;
			os << "        " << mangle(sig->name()) <<
			      "_mon.trigger(sim_);" << endl;

		  } else {
			os << "        " << mangle(cur->name()) <<
			      ".set(sim_, " << pin << ", " <<
			      rval << "[" << idx << "]);" << endl;
		  }
	    }
      }
}

void target_vvm::proc_block(ostream&os, const NetBlock*net)
{
      net->emit_recurse(os, this);
}

void target_vvm::proc_condit(ostream&os, const NetCondit*net)
{
      string expr = emit_proc_rval(os, net->expr());
      os << "        if (" << expr << "[0] == V1) {" << endl;
      net->emit_recurse_if(os, this);
      os << "        } else {" << endl;
      net->emit_recurse_else(os, this);
      os << "        }" << endl;
}

void target_vvm::proc_task(ostream&os, const NetTask*net)
{
      if (net->name()[0] == '$') {
	    string ptmp = make_temp();
	    os << "        struct vvm_calltf_parm " << ptmp << "[" <<
		  net->nparms() << "];" << endl;
	    for (unsigned idx = 0 ;  idx < net->nparms() ;  idx += 1)
		  if (net->parm(idx)) {
			string val = emit_parm_rval(os, net->parm(idx));
			os << "        " << ptmp << "[" << idx << "] = " <<
			      val << ";" << endl;
		  }
	    os << "        vvm_calltask(sim_, \"" << net->name() << "\", " <<
		  net->nparms() << ", " << ptmp << ");" << endl;
      } else {
	    os << "        // Huh? " << net->name() << endl;
      }
}

/*
 * Within a process, the proc_event is a statement that is blocked
 * until the event is signalled.
 */
void target_vvm::proc_event(ostream&os, const NetPEvent*proc)
{
      thread_step_ += 1;
      os << "        step_ = &step_" << thread_step_ << "_;" << endl;
      os << "        " << mangle(proc->name()) << ".wait(vvm_pevent::";
      switch (proc->edge()) {
	  case NetPEvent::ANYEDGE:
	    os << "ANYEDGE";
	    break;
	  case NetPEvent::POSEDGE:
	    os << "POSEDGE";
	    break;
	  case NetPEvent::NEGEDGE:
	    os << "NEGEDGE";
	    break;
      }
      os << ", this);" << endl;
      os << "      }" << endl;
      os << "      void step_" << thread_step_ << "_()" << endl;
      os << "      {" << endl;

      proc->emit_proc_recurse(os, this);
}

/*
 * A delay suspends the thread for a period of time.
 */
void target_vvm::proc_delay(ostream&os, const NetPDelay*proc)
{
      thread_step_ += 1;
      os << "        step_ = &step_" << thread_step_ << "_;" << endl;
      os << "        sim_->thread_delay(" << proc->delay() << ", this);"
	 << endl;
      os << "      }" << endl;
      os << "      void step_" << thread_step_ << "_()" << endl;
      os << "      {" << endl;

      proc->emit_proc_recurse(os, this);
}

void target_vvm::end_process(ostream&os, const NetProcTop*proc)
{
      if (proc->type() == NetProcTop::KALWAYS) {
	    os << "        step_ = &step_0_;" << endl;
	    os << "        step_0_(); // XXXX" << endl;
      } else {
	    os << "        step_ = 0;" << endl;
      }

      os << "      }" << endl;
      os << "};" << endl;
}


static target_vvm target_vvm_obj;

extern const struct target tgt_vvm = {
      "vvm",
      &target_vvm_obj
};
/*
 * $Log: t-vvm.cc,v $
 * Revision 1.3  1998/11/07 19:17:10  steve
 *  Calculate expression widths at elaboration time.
 *
 * Revision 1.2  1998/11/07 17:05:06  steve
 *  Handle procedural conditional, and some
 *  of the conditional expressions.
 *
 *  Elaborate signals and identifiers differently,
 *  allowing the netlist to hold signal information.
 *
 * Revision 1.1  1998/11/03 23:29:05  steve
 *  Introduce verilog to CVS.
 *
 */

