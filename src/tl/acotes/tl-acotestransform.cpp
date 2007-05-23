/*
    Mercurium C/C++ Compiler
    Copyright (C) 2006-2007 - Roger Ferrer Ibanez <roger.ferrer@bsc.es>
	Acotes Translation Phase
	Copyright (C) 2007 - David Rodenas Pico <david.rodenas@bsc.es>
    Barcelona Supercomputing Center - Centro Nacional de Supercomputacion
    Universitat Politecnica de Catalunya

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include "tl-acotestransform.hpp"

#if 0
#include <list>
#include <iostream>
#include <sstream>
#include <set>
#include <stack>
#include <vector>

#include <assert.h>

#include "tl-ast.hpp"
#include "tl-compilerphase.hpp"
#include "tl-functor.hpp"
#include "tl-pragmasupport.hpp"
#include "tl-scopelink.hpp"
#include "tl-source.hpp"
#include "tl-traverse.hpp"
#endif


#include <assert.h>
#include <stack>
#include <string>

#include "tl-pragmasupport.hpp"

#include "tl-targetinfo.hpp"
#include "tl-taskgroupinfo.hpp"
#include "tl-taskinfo.hpp"
#include "tl-transformmintakaoutline.hpp"
#include "tl-transformtargetreplace.hpp"
#include "tl-transformtaskdeclarestate.hpp"
#include "tl-transformtaskgroupdeclarestreams.hpp"
#include "tl-transformtaskgroupreplace.hpp" 
#include "tl-transformtaskoutline.hpp" 
#include "tl-transformtaskreplace.hpp" 

namespace TL 
{
	static const std::string ARG_NAME("acolib__arg");
	static const std::string STATE_NAME("acolib__state");
	static const std::string ERROR_NAME("acolib__error");
	static const std::string THREAD_NAME("acolib__thread");
	

	// AcotesTransform =========================================================
	class AcotesTransform : public PragmaCustomCompilerPhase 
	{
		// AcotesTransform fields ----------------------------------------------
		private:
		std::stack<TaskgroupInfo*>     _taskgroup_stack;
		std::stack<TaskInfo*>          _task_stack;


		public:
		// AcotesTransform constructor -----------------------------------------
		AcotesTransform()
		: PragmaCustomCompilerPhase("acotes")
		{
			// register parsing functions
			on_directive_pre["taskgroup"].connect(
				functor(&AcotesTransform::taskgroup_preorder, *this)
			);
			on_directive_pre["task"].connect(
				functor(&AcotesTransform::task_preorder, *this)
			);
			on_directive_post["target"].connect(
				functor(&AcotesTransform::target_postorder, *this)
			);
			on_directive_post["task"].connect(
				functor(&AcotesTransform::task_postorder, *this)
			);
			on_directive_post["taskgroup"].connect(
				functor(&AcotesTransform::taskgroup_postorder, *this)
			);
		}
		
		private:
		// taskgroup_preorder --------------------------------------------------
		void 
		taskgroup_preorder
				( PragmaCustomConstruct pragma_custom_construct
				)
		{
			// Instance a new taskgroup information for that taskgroup
			TaskgroupInfo* taskgroup_info= new TaskgroupInfo();
			TaskInfo* task_info_phantom= taskgroup_info->get_task_info_phantom();
			
			// Place the taskgroup on top of the stack
			_taskgroup_stack.push(taskgroup_info);
			// Pushes the phantom with no shortcuts
			_task_stack.push(task_info_phantom);


			ObjectList<IdExpression> vars;
			// Adds shortcuts to task information
			vars= pragma_custom_construct
					.get_clause("shortcut")
					.id_expressions();
			for		( ObjectList<IdExpression>::iterator it= vars.begin()
					; it != vars.end()
					; it++
					)
			{
				IdExpression var= *it;
				Symbol symbol= var.get_symbol();
				
				task_info_phantom->add_shortcut(symbol);
			} 

	
			// Enquees the replaces the task
			TransformTaskgroupReplace* transform_taskgroup_replace= 
					new TransformTaskgroupReplace
							( pragma_custom_construct
							, taskgroup_info
							);
			taskgroup_info->
					set_transform_taskgroup_replace
					( transform_taskgroup_replace
					);


			// Enquees the stream declaration
			TransformTaskgroupDeclareStreams* transform_task_replace= 
					new TransformTaskgroupDeclareStreams
							( pragma_custom_construct
							, taskgroup_info
							);
			transform_taskgroup_replace->
					add_previous_transform(transform_task_replace);
			
			// It has an implicit task
			//task_preorder(pragma_custom_construct);
					
		}

		// task_preorder -------------------------------------------------------
		void 
		task_preorder
				( PragmaCustomConstruct pragma_custom_construct
				)
		{
			// Retrieves the top taskgroup
			TaskgroupInfo* taskgroup_info= _taskgroup_stack.top();
			// Retrieves the parent for this task
			TaskInfo* task_info_parent= _task_stack.top();
			// Instance a new taskgroup information for that taskgroup
			TaskInfo* task_info= taskgroup_info->new_task_info();
			// Registers the current task as children of the parent
			task_info_parent->add_task_info_child(task_info);
			// Place the taskgroup on top of the stack
			_task_stack.push(task_info);

			ObjectList<IdExpression> vars;
			ObjectList<Expression> exprs;
			// Adds shortcuts to task information
			vars= pragma_custom_construct
					.get_clause("shortcut")
					.id_expressions();
			for		( ObjectList<IdExpression>::iterator it= vars.begin()
					; it != vars.end()
					; it++
					)
			{
				IdExpression var= *it;
				Symbol symbol= var.get_symbol();
				
				task_info->add_shortcut(symbol);
			} 
			// Adds inputs to task information
			vars= pragma_custom_construct
					.get_clause("input")
					.id_expressions();
			for		( ObjectList<IdExpression>::iterator it= vars.begin()
					; it != vars.end()
					; it++
					)
			{
				IdExpression var= *it;
				Symbol symbol= var.get_symbol();
				
				task_info->add_input(symbol);
			} 
			// Adds output to task information
			vars= pragma_custom_construct
					.get_clause("output")
					.id_expressions();
			for		( ObjectList<IdExpression>::iterator it= vars.begin()
					; it != vars.end()
					; it++
					)
			{
				IdExpression var= *it;
				Symbol symbol= var.get_symbol();
				
				task_info->add_output(symbol);
			} 
			// Adds inputs to task information
			vars= pragma_custom_construct
					.get_clause("import")
					.id_expressions();
			for		( ObjectList<IdExpression>::iterator it= vars.begin()
					; it != vars.end()
					; it++
					)
			{
				IdExpression var= *it;
				Symbol symbol= var.get_symbol();
				
				task_info->add_import(symbol);
			} 
			// Adds output to task information
			vars= pragma_custom_construct
					.get_clause("export")
					.id_expressions();
			for		( ObjectList<IdExpression>::iterator it= vars.begin()
					; it != vars.end()
					; it++
					)
			{
				IdExpression var= *it;
				Symbol symbol= var.get_symbol();
				
				task_info->add_export(symbol);
			} 
			// Adds private to task information
			vars= pragma_custom_construct
					.get_clause("private")
					.id_expressions();
			for		( ObjectList<IdExpression>::iterator it= vars.begin()
					; it != vars.end()
					; it++
					)
			{
				IdExpression var= *it;
				Symbol symbol= var.get_symbol();
				
				task_info->add_private(symbol);
			} 
			// Adds firstprivate to task information
			vars= pragma_custom_construct
					.get_clause("firstprivate")
					.id_expressions();
			for		( ObjectList<IdExpression>::iterator it= vars.begin()
					; it != vars.end()
					; it++
					)
			{
				IdExpression var= *it;
				Symbol symbol= var.get_symbol();
				
				task_info->add_firstprivate(symbol);
			} 
			// Adds lastprivate to task information
			vars= pragma_custom_construct
					.get_clause("lastprivate")
					.id_expressions();
			for		( ObjectList<IdExpression>::iterator it= vars.begin()
					; it != vars.end()
					; it++
					)
			{
				IdExpression var= *it;
				Symbol symbol= var.get_symbol();
				
				task_info->add_lastprivate(symbol);
			} 
			// Adds targets inputs
			exprs= pragma_custom_construct
					.get_clause("targetinput")
					.get_expression_list();
			if ((exprs.size() % 2) != 0)
			{
				std::cerr 
						<< "ERROR: task targetinput(symbol,label) must have "
						<< "one symbol and one label"
						<< std::endl;
				assert(0);
			}
			for		( ObjectList<Expression>::iterator it= exprs.begin()
					; it != exprs.end()
					; it++
					)
			{
				Expression var_expression= *it;
				IdExpression var= var_expression.get_id_expression();
				Symbol symbol= var.get_symbol();
				
				it++;
				Expression label_expression= *it;
				std::string label= label_expression.prettyprint();
				
				task_info->add_target_input(symbol, label);
			} 
			// Adds targets outputs
			exprs= pragma_custom_construct
					.get_clause("targetoutput")
					.get_expression_list();
			if ((exprs.size() % 2) != 0)
			{
				std::cerr 
						<< "ERROR: task targetoutput(symbol,label) must have "
						<< "one symbol and one label"
						<< std::endl;
				assert(0);
			}
			for		( ObjectList<Expression>::iterator it= exprs.begin()
					; it != exprs.end()
					; it++
					)
			{
				Expression var_expression= *it;
				IdExpression var= var_expression.get_id_expression();
				Symbol symbol= var.get_symbol();
				
				it++;
				Expression label_expression= *it;
				std::string label= label_expression.prettyprint();
				
				task_info->add_target_output(symbol, label);
			} 
		}

		// target_postorder ----------------------------------------------------
		void 
		target_postorder
				( PragmaCustomConstruct pragma_custom_construct
				)
		{
			// Retrieves the top taskgroup
			TaskgroupInfo* taskgroup_info= _taskgroup_stack.top();

			// Retrieves the top task
			TaskInfo* task_info= _task_stack.top();
			
			// Retrieves the label for that target
			ObjectList<Expression> exprs= pragma_custom_construct
					.get_clause("label")
					.get_expression_list()
					;
			if (exprs.size() != 1)
			{
				std::cerr
						<< "ERROR: #pragma acotes target directive requires "
						<< "one single label." 
						<< std::endl
						;
				assert(0);
			}
			Expression expr= (*exprs.begin());
			std::string label= expr.prettyprint();
			
			// Create a new instance to work
			TargetInfo* target_info= task_info->new_target_info(label);

			ObjectList<IdExpression> vars;
			// Adds inputs to task information
			vars= pragma_custom_construct
					.get_clause("input")
					.id_expressions();
			for		( ObjectList<IdExpression>::iterator it= vars.begin()
					; it != vars.end()
					; it++
					)
			{
				IdExpression var= *it;
				Symbol symbol= var.get_symbol();
				
				target_info->add_input(symbol);
			} 
			// Adds output to task information
			vars= pragma_custom_construct
					.get_clause("output")
					.id_expressions();
			for		( ObjectList<IdExpression>::iterator it= vars.begin()
					; it != vars.end()
					; it++
					)
			{
				IdExpression var= *it;
				Symbol symbol= var.get_symbol();
				
				target_info->add_output(symbol);
			}

			// Transformation support
			TransformTaskgroupReplace* transform_taskgroup=
					taskgroup_info->get_transform_taskgroup_replace(); 

			// Enquees the outline generation for that task
			TransformTargetReplace* transform_target_replace=
					new TransformTargetReplace
							(pragma_custom_construct
							, target_info
							);
			transform_taskgroup->add_previous_transform(transform_target_replace);
		}

		// task_postorder ------------------------------------------------------
		void 
		task_postorder
				( PragmaCustomConstruct pragma_custom_construct
				)
		{
			// Retrieves the top taskgroup
			TaskgroupInfo* taskgroup_info= _taskgroup_stack.top();

			// Retrieves the top task
			TaskInfo* task_info= _task_stack.top();
			
			// Retrieves the taskbody for that task
			//Statement task_body= pragma_custom_construct.get_statement();
			//task_info->set_body(task_body.prettyprint());
			
			// Transformation support
			TransformTaskgroupReplace* transform_taskgroup=
					taskgroup_info->get_transform_taskgroup_replace();
					 
			// Enquees the generation for that task state
			TransformTaskDeclareState* transform_task_define_state=
					new TransformTaskDeclareState
							( pragma_custom_construct
							, task_info
							);
			transform_taskgroup->add_previous_transform(transform_task_define_state);
			
			// Enquees the outline generation for that task
			TransformTaskOutline* transform_task_outline=
					new TransformTaskOutline
							(pragma_custom_construct
							, task_info
							);
			transform_taskgroup->add_previous_transform(transform_task_outline);
			
			// Enquees the replaces the task
			TransformTaskReplace* transform_task_replace= 
					new TransformTaskReplace
							( pragma_custom_construct
							, task_info
							);
			transform_taskgroup->add_previous_transform(transform_task_replace);
			
			// Removes the task from the top
			_task_stack.pop();
		}
 
 		// taskgroup_postorder -------------------------------------------------
		void 
		taskgroup_postorder
				( PragmaCustomConstruct pragma_custom_construct
				)
		{
			// Queries current the taskgroup
			TaskgroupInfo* taskgroup_info= _taskgroup_stack.top();

			// It has an implicit task
			//task_postorder(pragma_custom_construct);

			// Transformation support
			TransformTaskgroupReplace* transform_taskgroup=
					taskgroup_info->get_transform_taskgroup_replace();
					 
			// Enquees the mintaka services
			TransformMintakaOutline* transform_mintaka_outline= 
					new TransformMintakaOutline
							( pragma_custom_construct
							);
			transform_taskgroup->add_previous_transform(transform_mintaka_outline);


			// Compute graph
			taskgroup_info->compute_graph();

			// Apply all transformations
			transform_taskgroup->transform();


			// Popes the phantom with no shortcuts
			_task_stack.pop();
			// Deletes and popes the current taskgroup
			delete taskgroup_info;
			_taskgroup_stack.pop();
		}

	};

}


EXPORT_PHASE(TL::AcotesTransform);
